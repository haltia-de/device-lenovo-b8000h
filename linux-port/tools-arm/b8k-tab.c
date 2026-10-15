// SPDX-License-Identifier: GPL-2.0
/*
 * b8k-tab: hide the panel and leave a small tab to bring it back.
 *
 * Started by the button under the clock. It sends SIGUSR1 to waybar,
 * which then turns invisible and gives its width back to the windows,
 * and puts a 120x44 tab with a ">" on the overlay layer in the bottom
 * left corner: the same size and place as the button, so the tab sits
 * exactly where the finger just was. A tap on the tab buzzes, sends
 * SIGUSR1 again to show the panel, and the program ends.
 *
 * It also ends, showing the panel first, on SIGTERM or SIGINT, and ends
 * quietly when that waybar is gone (the session restarted it, visible),
 * so the tab can never leave the panel hidden for good. A second copy
 * exits at once. It is C because it lives on screen while the panel is
 * hidden, and a Python process would cost some 20 MB for one button.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#define W 120 /* the panel width */
#define H 44  /* the hide button: 40 px high plus 2 px margins */
#define BUZZ_MS 70
#define BG 0xff242424u    /* panel background */
#define PRESS 0xff3a3a3au /* the panel's pressed state */

static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct zwlr_layer_shell_v1 *layer_shell;
static struct wl_seat *seat;
static struct wl_surface *surface;
static struct wl_buffer *normal_buf, *pressed_buf;
static pid_t bar;
static int configured, tapped, running = 1;

/* --- the waybar process ------------------------------------------------ */
static int read_line(const char *path, char *buf, size_t len)
{
	FILE *f = fopen(path, "r");

	if (!f)
		return -1;
	if (!fgets(buf, (int)len, f)) {
		fclose(f);
		return -1;
	}
	fclose(f);
	buf[strcspn(buf, "\n")] = '\0';
	return 0;
}

/* A live waybar: its comm, and a state other than zombie. */
static int is_bar(pid_t pid)
{
	char path[64], line[128];
	FILE *f;
	int alive = 0;

	snprintf(path, sizeof(path), "/proc/%d/comm", (int)pid);
	if (read_line(path, line, sizeof(line)) || strcmp(line, "waybar"))
		return 0;
	snprintf(path, sizeof(path), "/proc/%d/status", (int)pid);
	f = fopen(path, "r");
	if (!f)
		return 0;
	while (fgets(line, sizeof(line), f)) {
		if (!strncmp(line, "State:", 6)) {
			alive = !strchr(line + 6, 'Z');
			break;
		}
	}
	fclose(f);
	return alive;
}

static pid_t find_bar(void)
{
	DIR *proc = opendir("/proc");
	struct dirent *de;
	pid_t found = 0;

	if (!proc)
		return 0;
	while ((de = readdir(proc))) {
		pid_t pid = (pid_t)atoi(de->d_name);

		if (pid > 0 && is_bar(pid)) {
			found = pid;
			break;
		}
	}
	closedir(proc);
	return found;
}

/* --- vibration, as in b8k-press ------------------------------------------ */
static void buzz(void)
{
	DIR *dir = opendir("/sys/class/input");
	struct dirent *de;
	char node[64] = "";
	struct ff_effect effect;
	struct input_event play;
	int fd;

	if (!dir)
		return;
	while ((de = readdir(dir))) {
		char path[300], name[128];

		if (strncmp(de->d_name, "event", 5))
			continue;
		snprintf(path, sizeof(path), "/sys/class/input/%s/device/name", de->d_name);
		if (!read_line(path, name, sizeof(name)) && !strcmp(name, "regulator-haptic")) {
			snprintf(node, sizeof(node), "/dev/input/%s", de->d_name);
			break;
		}
	}
	closedir(dir);
	if (!node[0])
		return;
	fd = open(node, O_RDWR | O_CLOEXEC);
	if (fd < 0)
		return;
	memset(&effect, 0, sizeof(effect));
	effect.type = FF_RUMBLE;
	effect.id = -1;
	effect.u.rumble.strong_magnitude = 0xffff;
	effect.replay.length = BUZZ_MS;
	if (ioctl(fd, EVIOCSFF, &effect) == 0) {
		struct timespec ts = { 0, (BUZZ_MS + 5) * 1000000L };

		memset(&play, 0, sizeof(play));
		play.type = EV_FF;
		play.code = (unsigned short)effect.id;
		play.value = 1;
		if (write(fd, &play, sizeof(play)) == (ssize_t)sizeof(play))
			nanosleep(&ts, NULL); /* the effect ends when the fd closes */
	}
	close(fd);
}

/* --- drawing ------------------------------------------------------------- */
static float seg_dist(float px, float py, float ax, float ay, float bx, float by)
{
	float dx = bx - ax, dy = by - ay;
	float t = ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy);

	if (t < 0)
		t = 0;
	if (t > 1)
		t = 1;
	return hypotf(px - ax - t * dx, py - ay - t * dy);
}

static uint32_t mix(uint32_t bg, float a)
{
	uint32_t r = (bg >> 16) & 0xff, g = (bg >> 8) & 0xff, b = bg & 0xff;

	r = (uint32_t)(r + (255 - r) * a);
	g = (uint32_t)(g + (255 - g) * a);
	b = (uint32_t)(b + (255 - b) * a);
	return 0xff000000u | r << 16 | g << 8 | b;
}

/*
 * The panel look: dark ground, a white ">" the size of the 30 px hide
 * icon (hide.svg mirrored), and when pressed the 12 px rounded lighter
 * box of the panel's .pressed class.
 */
static void draw(uint32_t *px, int pressed)
{
	/* hide.svg is "M20 12 L32 24 L20 36" mirrored, 48 -> 30 px, width 5. */
	const float s = 30.0f / 48.0f, ox = W / 2.0f - 15.0f, oy = H / 2.0f - 15.0f;
	const float ax = ox + 20 * s, ay = oy + 12 * s, bx = ox + 30 * s, by = oy + 24 * s;
	const float cx = ox + 20 * s, cy = oy + 36 * s, half = 5 * s / 2;

	for (int y = 0; y < H; y++) {
		for (int x = 0; x < W; x++) {
			uint32_t bg = BG;
			float fx = x + 0.5f, fy = y + 0.5f, d, a;

			if (pressed && x >= 4 && x < W - 4 && y >= 2 && y < H - 2) {
				/* rounded corners of the 112x40 box */
				float rx = fx < 16 ? 16 - fx : (fx > W - 16 ? fx - (W - 16) : 0);
				float ry = fy < 14 ? 14 - fy : (fy > H - 14 ? fy - (H - 14) : 0);

				if (rx * rx + ry * ry <= 144)
					bg = PRESS;
			}
			d = fminf(seg_dist(fx, fy, ax, ay, bx, by), seg_dist(fx, fy, bx, by, cx, cy));
			a = half + 0.5f - d;
			a = a < 0 ? 0 : (a > 1 ? 1 : a);
			px[y * W + x] = a > 0 ? mix(bg, a) : bg;
		}
	}
}

static struct wl_buffer *make_buffer(int pressed)
{
	int size = W * H * 4;
	int fd = memfd_create("b8k-tab", MFD_CLOEXEC);
	struct wl_shm_pool *pool;
	struct wl_buffer *buf;
	void *data;

	if (fd < 0 || ftruncate(fd, size) < 0)
		return NULL;
	data = mmap(NULL, (size_t)size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (data == MAP_FAILED) {
		close(fd);
		return NULL;
	}
	draw(data, pressed);
	munmap(data, (size_t)size);
	pool = wl_shm_create_pool(shm, fd, size);
	buf = wl_shm_pool_create_buffer(pool, 0, W, H, W * 4, WL_SHM_FORMAT_XRGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);
	return buf;
}

static void show(struct wl_buffer *buf)
{
	wl_surface_attach(surface, buf, 0, 0);
	wl_surface_damage(surface, 0, 0, W, H);
	wl_surface_commit(surface);
}

/* --- input --------------------------------------------------------------- */
static void press(void)
{
	show(pressed_buf);
	wl_display_flush(wl_proxy_get_display((struct wl_proxy *)surface));
}

static void release(void)
{
	tapped = 1;
	running = 0;
}

static void touch_down(void *d, struct wl_touch *t, uint32_t serial, uint32_t time,
		       struct wl_surface *s, int32_t id, wl_fixed_t x, wl_fixed_t y)
{
	(void)d; (void)t; (void)serial; (void)time; (void)s; (void)id; (void)x; (void)y;
	press();
}

static void touch_up(void *d, struct wl_touch *t, uint32_t serial, uint32_t time, int32_t id)
{
	(void)d; (void)t; (void)serial; (void)time; (void)id;
	release();
}

static void touch_motion(void *d, struct wl_touch *t, uint32_t time, int32_t id,
			 wl_fixed_t x, wl_fixed_t y)
{
	(void)d; (void)t; (void)time; (void)id; (void)x; (void)y;
}

static void touch_frame(void *d, struct wl_touch *t) { (void)d; (void)t; }

static void touch_cancel(void *d, struct wl_touch *t)
{
	(void)d; (void)t;
	show(normal_buf);
}

static void touch_shape(void *d, struct wl_touch *t, int32_t id, wl_fixed_t a, wl_fixed_t b)
{
	(void)d; (void)t; (void)id; (void)a; (void)b;
}

static void touch_orientation(void *d, struct wl_touch *t, int32_t id, wl_fixed_t o)
{
	(void)d; (void)t; (void)id; (void)o;
}

static const struct wl_touch_listener touch_listener = {
	.down = touch_down,
	.up = touch_up,
	.motion = touch_motion,
	.frame = touch_frame,
	.cancel = touch_cancel,
	.shape = touch_shape,
	.orientation = touch_orientation,
};

static void ptr_enter(void *d, struct wl_pointer *p, uint32_t serial, struct wl_surface *s,
		      wl_fixed_t x, wl_fixed_t y)
{
	(void)d; (void)p; (void)serial; (void)s; (void)x; (void)y;
}

static void ptr_leave(void *d, struct wl_pointer *p, uint32_t serial, struct wl_surface *s)
{
	(void)d; (void)p; (void)serial; (void)s;
}

static void ptr_motion(void *d, struct wl_pointer *p, uint32_t time, wl_fixed_t x, wl_fixed_t y)
{
	(void)d; (void)p; (void)time; (void)x; (void)y;
}

static void ptr_button(void *d, struct wl_pointer *p, uint32_t serial, uint32_t time,
		       uint32_t button, uint32_t state)
{
	(void)d; (void)p; (void)serial; (void)time; (void)button;
	if (state == WL_POINTER_BUTTON_STATE_PRESSED)
		press();
	else
		release();
}

static void ptr_axis(void *d, struct wl_pointer *p, uint32_t time, uint32_t axis, wl_fixed_t v)
{
	(void)d; (void)p; (void)time; (void)axis; (void)v;
}

static void ptr_frame(void *d, struct wl_pointer *p) { (void)d; (void)p; }

static void ptr_axis_source(void *d, struct wl_pointer *p, uint32_t src)
{
	(void)d; (void)p; (void)src;
}

static void ptr_axis_stop(void *d, struct wl_pointer *p, uint32_t time, uint32_t axis)
{
	(void)d; (void)p; (void)time; (void)axis;
}

static void ptr_axis_discrete(void *d, struct wl_pointer *p, uint32_t axis, int32_t v)
{
	(void)d; (void)p; (void)axis; (void)v;
}

static const struct wl_pointer_listener pointer_listener = {
	.enter = ptr_enter,
	.leave = ptr_leave,
	.motion = ptr_motion,
	.button = ptr_button,
	.axis = ptr_axis,
	.frame = ptr_frame,
	.axis_source = ptr_axis_source,
	.axis_stop = ptr_axis_stop,
	.axis_discrete = ptr_axis_discrete,
};

static void seat_caps(void *d, struct wl_seat *s, uint32_t caps)
{
	static struct wl_touch *touch;
	static struct wl_pointer *pointer;

	(void)d;
	if ((caps & WL_SEAT_CAPABILITY_TOUCH) && !touch) {
		touch = wl_seat_get_touch(s);
		wl_touch_add_listener(touch, &touch_listener, NULL);
	}
	if ((caps & WL_SEAT_CAPABILITY_POINTER) && !pointer) {
		pointer = wl_seat_get_pointer(s);
		wl_pointer_add_listener(pointer, &pointer_listener, NULL);
	}
}

static void seat_name(void *d, struct wl_seat *s, const char *name)
{
	(void)d; (void)s; (void)name;
}

static const struct wl_seat_listener seat_listener = {
	.capabilities = seat_caps,
	.name = seat_name,
};

/* --- globals and the layer surface ---------------------------------------- */
static void global(void *d, struct wl_registry *r, uint32_t name, const char *iface,
		   uint32_t version)
{
	(void)d;
	if (!strcmp(iface, wl_compositor_interface.name))
		compositor = wl_registry_bind(r, name, &wl_compositor_interface, 4);
	else if (!strcmp(iface, wl_shm_interface.name))
		shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
	else if (!strcmp(iface, zwlr_layer_shell_v1_interface.name))
		layer_shell = wl_registry_bind(r, name, &zwlr_layer_shell_v1_interface,
					       version < 4 ? version : 4);
	else if (!strcmp(iface, wl_seat_interface.name) && !seat) {
		/* v5 at most: the listeners above cover every event up to it */
		seat = wl_registry_bind(r, name, &wl_seat_interface, version < 5 ? version : 5);
		wl_seat_add_listener(seat, &seat_listener, NULL);
	}
}

static void global_remove(void *d, struct wl_registry *r, uint32_t name)
{
	(void)d; (void)r; (void)name;
}

static const struct wl_registry_listener registry_listener = {
	.global = global,
	.global_remove = global_remove,
};

static void layer_configure(void *d, struct zwlr_layer_surface_v1 *ls, uint32_t serial,
			    uint32_t w, uint32_t h)
{
	(void)d; (void)w; (void)h;
	zwlr_layer_surface_v1_ack_configure(ls, serial);
	if (!configured) {
		configured = 1;
		show(normal_buf);
	}
}

static void layer_closed(void *d, struct zwlr_layer_surface_v1 *ls)
{
	(void)d; (void)ls;
	running = 0;
}

static const struct zwlr_layer_surface_v1_listener layer_listener = {
	.configure = layer_configure,
	.closed = layer_closed,
};

int main(void)
{
	const char *run = getenv("XDG_RUNTIME_DIR");
	char lockpath[256];
	struct wl_display *display;
	struct zwlr_layer_surface_v1 *ls;
	struct itimerspec every = { { 1, 0 }, { 1, 0 } };
	sigset_t mask;
	int lock, sfd, tfd;

	if (!run || run[0] != '/')
		return 1;
	/* One tab at a time: a second tap on a stale button does nothing. */
	snprintf(lockpath, sizeof(lockpath), "%s/b8k-tab.lock", run);
	lock = open(lockpath, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (lock < 0 || flock(lock, LOCK_EX | LOCK_NB))
		return 0;

	bar = find_bar();
	if (!bar)
		return 1;
	display = wl_display_connect(NULL);
	if (!display)
		return 1;
	wl_registry_add_listener(wl_display_get_registry(display), &registry_listener, NULL);
	wl_display_roundtrip(display);
	if (!compositor || !shm || !layer_shell)
		return 1;
	normal_buf = make_buffer(0);
	pressed_buf = make_buffer(1);
	if (!normal_buf || !pressed_buf)
		return 1;

	sigemptyset(&mask);
	sigaddset(&mask, SIGTERM);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGHUP);
	sigprocmask(SIG_BLOCK, &mask, NULL);
	sfd = signalfd(-1, &mask, SFD_CLOEXEC);
	tfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
	timerfd_settime(tfd, 0, &every, NULL);

	/* Hide the panel, then put the tab where its button was. */
	kill(bar, SIGUSR1);
	surface = wl_compositor_create_surface(compositor);
	ls = zwlr_layer_shell_v1_get_layer_surface(layer_shell, surface, NULL,
						   ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "b8k-tab");
	zwlr_layer_surface_v1_add_listener(ls, &layer_listener, NULL);
	zwlr_layer_surface_v1_set_size(ls, W, H);
	zwlr_layer_surface_v1_set_anchor(ls, ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
					     ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
	/* -1: at the screen edge, whatever other surfaces reserve. */
	zwlr_layer_surface_v1_set_exclusive_zone(ls, -1);
	wl_surface_commit(surface);

	while (running) {
		struct pollfd fds[3] = {
			{ wl_display_get_fd(display), POLLIN, 0 },
			{ sfd, POLLIN, 0 },
			{ tfd, POLLIN, 0 },
		};

		while (wl_display_prepare_read(display) != 0)
			wl_display_dispatch_pending(display);
		wl_display_flush(display);
		if (poll(fds, 3, -1) < 0 && errno != EINTR) {
			wl_display_cancel_read(display);
			break;
		}
		if (fds[0].revents & POLLIN) {
			if (wl_display_read_events(display) < 0)
				break;
		} else {
			wl_display_cancel_read(display);
		}
		if (wl_display_dispatch_pending(display) < 0)
			break;
		if (fds[1].revents & POLLIN) {
			/* Stopped from outside: never leave the panel hidden. */
			kill(bar, SIGUSR1);
			return 0;
		}
		if (fds[2].revents & POLLIN) {
			uint64_t n;

			if (read(tfd, &n, sizeof(n)) < 0)
				n = 0;
			/* A new waybar starts visible; this tab has nothing to bring back. */
			if (!is_bar(bar))
				return 0;
		}
	}
	if (tapped) {
		buzz();
		if (is_bar(bar))
			kill(bar, SIGUSR1);
	} else if (is_bar(bar)) {
		/* The compositor closed the tab, or the connection broke. */
		kill(bar, SIGUSR1);
	}
	zwlr_layer_surface_v1_destroy(ls);
	wl_surface_destroy(surface);
	wl_display_flush(display);
	wl_display_disconnect(display);
	return 0;
}
