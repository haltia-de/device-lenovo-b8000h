// SPDX-License-Identifier: GPL-2.0
/*
 * b8k-close: the one way to close a program, a "✕" in the top right corner.
 *
 *   b8k-close            run it (b8k-launcher starts one per session)
 *   b8k-close update     look again at what is in front (b8k-focus and
 *                        b8k-home call this when it changes)
 *
 * A square on the overlay layer at the top right of the screen, drawn as
 * the last button of the program's header (same size, place and look as
 * the shell's header buttons, which end short of it), red only while
 * pressed. It shows while a program's window is in front ($XDG_RUNTIME_DIR/
 * b8k-launch/front names its app id; b8k-focus writes it) or a program is
 * starting under its curtain (b8k-launch/starting), and the home screen is
 * not up; otherwise it is clear and lets taps through. A tap runs b8k-ask
 * close-front: a starting program is stopped at once, a window is closed
 * after "Закрыть «…»?" with Отмена preselected, as a program closed by
 * accident is lost work for an older hand. The same corner for every
 * program, the video player included.
 */
#define _GNU_SOURCE
#include <cairo.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <wayland-client.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

/* The shell's header buttons fill its 88 px from 8 px down, 6 px apart,
 * the last 16 px from the right edge (b8k_ui.HEADER_CSS pads the header
 * 110 px on the right for this one). */
#define SQ 88
#define TOP 8
#define RIGHT 16
#define W (SQ + RIGHT + 6)
#define H (TOP + SQ + 8)
#define POLL_S 2 /* a look at the files now and then, besides the signal */

extern char **environ;

static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct wl_seat *seat;
static struct zwlr_layer_shell_v1 *layer_shell;
static struct wl_surface *surface;
static struct zwlr_layer_surface_v1 *layer;
static struct wl_touch *touch;
static struct wl_pointer *pointer;
static int configured, running = 1, shown = -1, want, pressed;
static int32_t touch_id = -1;

struct buffer {
	struct wl_buffer *wl;
	void *data;
	int busy;
};
static struct buffer bufs[2];

static const char *runtime_dir(void)
{
	const char *run = getenv("XDG_RUNTIME_DIR");

	return run && run[0] == '/' ? run : NULL;
}

/* Something to close is in front: a front app id, and no home screen up. */
static int wanted(void)
{
	const char *run = runtime_dir();
	char path[512], id[256] = "";
	struct stat st;
	FILE *f;

	if (!run)
		return 0;
	snprintf(path, sizeof(path), "%s/b8k-home.raised", run);
	if (!stat(path, &st))
		return 0;
	snprintf(path, sizeof(path), "%s/b8k-launch/starting", run);
	if (!stat(path, &st))
		return 1; /* a program under its curtain: the way to stop it */
	snprintf(path, sizeof(path), "%s/b8k-launch/front", run);
	if (!(f = fopen(path, "r")))
		return 0;
	if (!fgets(id, sizeof(id), f))
		id[0] = '\0';
	fclose(f);
	return id[0] && id[0] != '\n';
}

static void spawn(const char *a0, const char *a1)
{
	char *argv[] = { (char *)a0, (char *)a1, NULL };
	posix_spawnattr_t attr;
	sigset_t none, all;
	pid_t pid;

	/* A clean signal mask: ours blocks signals for the signalfd. */
	sigemptyset(&none);
	sigfillset(&all);
	posix_spawnattr_init(&attr);
	posix_spawnattr_setsigmask(&attr, &none);
	posix_spawnattr_setsigdefault(&attr, &all);
	posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);
	posix_spawn(&pid, a0, NULL, &attr, argv, environ);
	posix_spawnattr_destroy(&attr);
}

static void buffer_release(void *d, struct wl_buffer *b)
{
	(void)b;
	((struct buffer *)d)->busy = 0;
}

static const struct wl_buffer_listener buffer_listener = { .release = buffer_release };

static int make_buffers(void)
{
	size_t size = (size_t)W * H * 4;

	for (int i = 0; i < 2; i++) {
		int fd = memfd_create("b8k-close", MFD_CLOEXEC);
		struct wl_shm_pool *pool;

		if (fd < 0 || ftruncate(fd, (off_t)size) < 0)
			return -1;
		bufs[i].data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		if (bufs[i].data == MAP_FAILED) {
			close(fd);
			return -1;
		}
		pool = wl_shm_create_pool(shm, fd, (int32_t)size);
		bufs[i].wl = wl_shm_pool_create_buffer(pool, 0, W, H, W * 4,
						       WL_SHM_FORMAT_ARGB8888);
		wl_buffer_add_listener(bufs[i].wl, &buffer_listener, &bufs[i]);
		wl_shm_pool_destroy(pool);
		close(fd);
	}
	return 0;
}

static void rounded(cairo_t *cr, double x, double y, double w, double h, double r)
{
	cairo_new_sub_path(cr);
	cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
	cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
	cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
	cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
	cairo_close_path(cr);
}

static void draw(void *px, int on)
{
	cairo_surface_t *s = cairo_image_surface_create_for_data(px, CAIRO_FORMAT_ARGB32, W, H,
								  W * 4);
	cairo_t *cr = cairo_create(s);
	double x = W - SQ - RIGHT, y = TOP, c = SQ * 0.2;

	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_rgba(cr, 0, 0, 0, 0);
	cairo_paint(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
	if (on) {
		/* As the header buttons: #f2f2f0 with a #c0bfbc line, the mark in
		 * #2e3436; red with a white mark only under the finger. */
		rounded(cr, x + 0.5, y + 0.5, SQ - 1, SQ - 1, 16);
		if (pressed)
			cairo_set_source_rgb(cr, 0xe0 / 255.0, 0x1b / 255.0, 0x24 / 255.0);
		else
			cairo_set_source_rgb(cr, 0xf2 / 255.0, 0xf2 / 255.0, 0xf0 / 255.0);
		cairo_fill_preserve(cr);
		if (pressed)
			cairo_set_source_rgb(cr, 0xe0 / 255.0, 0x1b / 255.0, 0x24 / 255.0);
		else
			cairo_set_source_rgb(cr, 0xc0 / 255.0, 0xbf / 255.0, 0xbc / 255.0);
		cairo_set_line_width(cr, 1);
		cairo_stroke(cr);
		if (pressed)
			cairo_set_source_rgb(cr, 1, 1, 1);
		else
			cairo_set_source_rgb(cr, 0x2e / 255.0, 0x34 / 255.0, 0x36 / 255.0);
		cairo_set_line_width(cr, 5);
		cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
		cairo_move_to(cr, x + SQ / 2.0 - c, y + SQ / 2.0 - c);
		cairo_line_to(cr, x + SQ / 2.0 + c, y + SQ / 2.0 + c);
		cairo_move_to(cr, x + SQ / 2.0 + c, y + SQ / 2.0 - c);
		cairo_line_to(cr, x + SQ / 2.0 - c, y + SQ / 2.0 + c);
		cairo_stroke(cr);
	}
	cairo_destroy(cr);
	cairo_surface_flush(s);
	cairo_surface_destroy(s);
}

/* Show or clear the button; a clear one takes no input. */
static void render(int on)
{
	struct buffer *b = !bufs[0].busy ? &bufs[0] : (!bufs[1].busy ? &bufs[1] : NULL);
	struct wl_region *region;

	if (!configured || !b)
		return;
	draw(b->data, on);
	b->busy = 1;
	region = wl_compositor_create_region(compositor);
	if (on)
		wl_region_add(region, 0, 0, W, H);
	wl_surface_set_input_region(surface, region);
	wl_region_destroy(region);
	wl_surface_attach(surface, b->wl, 0, 0);
	wl_surface_damage(surface, 0, 0, W, H);
	wl_surface_commit(surface);
	shown = on;
}

static void update(void)
{
	want = wanted();
	if (want != shown)
		render(want);
}

static void tapped(void)
{
	pressed = 0;
	if (shown == 1) {
		spawn("/usr/local/bin/b8k-press", "close");
		spawn("/usr/local/bin/b8k-ask", "close-front");
	}
	render(shown == 1);
}

static void touch_down(void *d, struct wl_touch *t, uint32_t serial, uint32_t time,
		       struct wl_surface *s, int32_t id, wl_fixed_t x, wl_fixed_t y)
{
	(void)d; (void)t; (void)serial; (void)time; (void)s; (void)x; (void)y;
	if (touch_id >= 0)
		return;
	touch_id = id;
	pressed = 1;
	render(shown == 1);
}

static void touch_up(void *d, struct wl_touch *t, uint32_t serial, uint32_t time, int32_t id)
{
	(void)d; (void)t; (void)serial; (void)time;
	if (id != touch_id)
		return;
	touch_id = -1;
	tapped(); /* on the lift of the finger, not the touch */
}

static void touch_motion(void *d, struct wl_touch *t, uint32_t time, int32_t id, wl_fixed_t x,
			 wl_fixed_t y)
{
	(void)d; (void)t; (void)time; (void)id; (void)x; (void)y;
}

static void touch_frame(void *d, struct wl_touch *t)
{
	(void)d; (void)t;
}

static void touch_cancel(void *d, struct wl_touch *t)
{
	(void)d; (void)t;
	touch_id = -1;
	pressed = 0;
	render(shown == 1);
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
	touch_down, touch_up, touch_motion, touch_frame, touch_cancel, touch_shape,
	touch_orientation,
};

static void ptr_enter(void *d, struct wl_pointer *p, uint32_t serial, struct wl_surface *s,
		      wl_fixed_t x, wl_fixed_t y)
{
	(void)d; (void)p; (void)serial; (void)s; (void)x; (void)y;
}

static void ptr_leave(void *d, struct wl_pointer *p, uint32_t serial, struct wl_surface *s)
{
	(void)d; (void)p; (void)serial; (void)s;
	pressed = 0;
}

static void ptr_motion(void *d, struct wl_pointer *p, uint32_t time, wl_fixed_t x, wl_fixed_t y)
{
	(void)d; (void)p; (void)time; (void)x; (void)y;
}

static void ptr_button(void *d, struct wl_pointer *p, uint32_t serial, uint32_t time,
		       uint32_t button, uint32_t state)
{
	(void)d; (void)p; (void)serial; (void)time;
	if (button != 0x110)
		return;
	if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
		pressed = 1;
		render(shown == 1);
	} else if (pressed) {
		tapped();
	}
}

static void ptr_axis(void *d, struct wl_pointer *p, uint32_t time, uint32_t axis, wl_fixed_t v)
{
	(void)d; (void)p; (void)time; (void)axis; (void)v;
}

static const struct wl_pointer_listener pointer_listener = {
	.enter = ptr_enter,
	.leave = ptr_leave,
	.motion = ptr_motion,
	.button = ptr_button,
	.axis = ptr_axis,
};

static void seat_caps(void *d, struct wl_seat *s, uint32_t caps)
{
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

static const struct wl_seat_listener seat_listener = { seat_caps, seat_name };

static void global(void *d, struct wl_registry *r, uint32_t name, const char *iface,
		   uint32_t version)
{
	(void)d;
	if (!strcmp(iface, wl_compositor_interface.name))
		compositor = wl_registry_bind(r, name, &wl_compositor_interface, 4);
	else if (!strcmp(iface, wl_shm_interface.name))
		shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
	else if (!strcmp(iface, wl_seat_interface.name) && !seat) {
		seat = wl_registry_bind(r, name, &wl_seat_interface, version < 5 ? version : 5);
		wl_seat_add_listener(seat, &seat_listener, NULL);
	} else if (!strcmp(iface, zwlr_layer_shell_v1_interface.name))
		layer_shell = wl_registry_bind(r, name, &zwlr_layer_shell_v1_interface,
					       version < 4 ? version : 4);
}

static void global_remove(void *d, struct wl_registry *r, uint32_t name)
{
	(void)d; (void)r; (void)name;
}

static const struct wl_registry_listener registry_listener = { global, global_remove };

static void layer_configure(void *d, struct zwlr_layer_surface_v1 *ls, uint32_t serial,
			    uint32_t w, uint32_t h)
{
	(void)d; (void)w; (void)h;
	zwlr_layer_surface_v1_ack_configure(ls, serial);
	if (!configured) {
		configured = 1;
		if (make_buffers() < 0) {
			running = 0;
			return;
		}
		shown = -1;
		update();
	}
}

static void layer_closed(void *d, struct zwlr_layer_surface_v1 *ls)
{
	(void)d; (void)ls;
	running = 0;
}

static const struct zwlr_layer_surface_v1_listener layer_listener = {
	layer_configure, layer_closed,
};

static int pid_path(char *out, size_t n)
{
	const char *run = runtime_dir();

	if (!run)
		return -1;
	snprintf(out, n, "%s/b8k-close.pid", run);
	return 0;
}

int main(int argc, char **argv)
{
	struct wl_display *display;
	struct itimerspec every = { { POLL_S, 0 }, { POLL_S, 0 } };
	char path[512], buf[32];
	sigset_t mask;
	int sfd, tfd, lock;

	if (argc > 1) {
		/* b8k-close update: tell the running one to look again. */
		FILE *f;

		if (strcmp(argv[1], "update")) {
			fprintf(stderr, "usage: b8k-close [update]\n");
			return 2;
		}
		if (pid_path(path, sizeof(path)) < 0 || !(f = fopen(path, "r")))
			return 1;
		if (!fgets(buf, sizeof(buf), f))
			buf[0] = '\0';
		fclose(f);
		return atoi(buf) > 1 && kill(atoi(buf), SIGUSR1) == 0 ? 0 : 1;
	}
	if (pid_path(path, sizeof(path)) < 0)
		return 1;
	lock = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
	if (lock < 0 || flock(lock, LOCK_EX | LOCK_NB) < 0)
		return 0; /* one per session */
	if (ftruncate(lock, 0) == 0) {
		snprintf(buf, sizeof(buf), "%d\n", getpid());
		if (write(lock, buf, strlen(buf)) < 0)
			perror("b8k-close: pid file");
	}

	signal(SIGCHLD, SIG_IGN);
	sigemptyset(&mask);
	sigaddset(&mask, SIGTERM);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGUSR1);
	sigprocmask(SIG_BLOCK, &mask, NULL);
	sfd = signalfd(-1, &mask, SFD_CLOEXEC);

	display = wl_display_connect(NULL);
	if (!display)
		return 1;
	wl_registry_add_listener(wl_display_get_registry(display), &registry_listener, NULL);
	wl_display_roundtrip(display);
	if (!compositor || !shm || !layer_shell)
		return 1;
	surface = wl_compositor_create_surface(compositor);
	layer = zwlr_layer_shell_v1_get_layer_surface(layer_shell, surface, NULL,
						      ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "b8k-close");
	zwlr_layer_surface_v1_add_listener(layer, &layer_listener, NULL);
	zwlr_layer_surface_v1_set_size(layer, W, H);
	zwlr_layer_surface_v1_set_anchor(layer, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
					 ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
	zwlr_layer_surface_v1_set_exclusive_zone(layer, -1); /* over everything, the keyboard too */
	wl_surface_commit(surface);

	tfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
	timerfd_settime(tfd, 0, &every, NULL);

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
			struct signalfd_siginfo si;

			if (read(sfd, &si, sizeof(si)) == sizeof(si) && si.ssi_signo != SIGUSR1)
				running = 0;
			update();
		}
		if (fds[2].revents & POLLIN) {
			uint64_t n;

			if (read(tfd, &n, sizeof(n)) < 0)
				n = 0;
			update();
		}
	}
	zwlr_layer_surface_v1_destroy(layer);
	wl_surface_destroy(surface);
	wl_display_disconnect(display);
	unlink(path);
	return 0;
}
