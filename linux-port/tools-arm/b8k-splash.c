// SPDX-License-Identifier: GPL-2.0
/*
 * b8k-splash: the curtain shown while a program opens.
 *
 *   b8k-splash [ID]
 *
 * b8k-launch starts it when a program has no window yet and stops it
 * (SIGTERM) once the window is there. It covers the screen right of the
 * panel on the top layer: the program's icon and name from apps.conf
 * (the "icon" and "label" keys of section ID), and a ring of dots that
 * turns while the program starts. Without an ID, or an icon, it shows
 * "Загрузка" alone.
 *
 * It replaces a Python version that took about half a second to show,
 * a good part of what it was there to cover. PNG icons go through cairo;
 * SVG ones through librsvg, opened with dlopen because the build sysroot
 * has no librsvg headers (without it the icon is left out).
 */
#define _GNU_SOURCE
#include <cairo.h>
#include <dlfcn.h>
#include <errno.h>
#include <math.h>
#include <pango/pangocairo.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <wayland-client.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#define APPS "/usr/local/share/b8k/apps.conf"
#define BAR 120        /* the panel on the left edge */
#define ICON 160       /* icon size, px */
#define DOTS 10
#define RING 86        /* ring radius */
#define STEP_MS 280
/* b8k-launch waits at most 40 s for a window; past this the splash goes by
 * itself, so a launcher that died can never leave it over the screen. */
#define LIFETIME_STEPS (45000 / STEP_MS)

static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct zwlr_layer_shell_v1 *layer_shell;
static struct wl_surface *surface;
static struct zwlr_layer_surface_v1 *layer;

static int width, height, configured, running = 1, step;
static char label[128] = "Загрузка";
static char icon_path[512];
static cairo_surface_t *icon; /* the icon drawn once at ICON x ICON */

struct buffer {
	struct wl_buffer *wl;
	void *data;
	size_t size;
	int busy;
};
static struct buffer bufs[2];

/* --- apps.conf ------------------------------------------------------------ */
static void trim(char *s)
{
	char *e = s + strlen(s);

	while (e > s && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t'))
		*--e = '\0';
}

/* The label and icon of section [id]. */
static void read_app(const char *id)
{
	const char *run = getenv("XDG_RUNTIME_DIR");
	char own[512], line[640], want[96];
	FILE *f = NULL;
	int inside = 0;

	/* The session's copy written by b8k-apps (one's own programs), else
	 * the built-in one. */
	if (run && run[0] == '/') {
		snprintf(own, sizeof(own), "%s/b8k/apps.conf", run);
		f = fopen(own, "r");
	}
	if (!f)
		f = fopen(APPS, "r");
	if (!f)
		return;
	snprintf(want, sizeof(want), "[%s]", id);
	while (fgets(line, sizeof(line), f)) {
		char *eq, *val;

		trim(line);
		if (line[0] == '[') {
			inside = !strcmp(line, want);
			continue;
		}
		if (!inside || !(eq = strchr(line, '=')))
			continue;
		val = eq + 1;
		while (*val == ' ' || *val == '\t')
			val++;
		*eq = '\0';
		trim(line);
		if (!strcmp(line, "label"))
			snprintf(label, sizeof(label), "%s", val);
		else if (!strcmp(line, "icon"))
			snprintf(icon_path, sizeof(icon_path), "%s", val);
	}
	fclose(f);
}

/* --- the icon --------------------------------------------------------------- */
typedef struct {
	double x, y, width, height;
} rsvg_rect;
typedef void *(*rsvg_new_fn)(const char *, void **);
typedef int (*rsvg_render_fn)(void *, cairo_t *, const rsvg_rect *, void **);
typedef void (*unref_fn)(void *);

static cairo_surface_t *load_svg(const char *path)
{
	void *lib = dlopen("librsvg-2.so.2", RTLD_NOW | RTLD_LOCAL);
	void *gobj = dlopen("libgobject-2.0.so.0", RTLD_NOW | RTLD_LOCAL);
	rsvg_new_fn new_from_file;
	rsvg_render_fn render;
	unref_fn unref;
	cairo_surface_t *out;
	cairo_t *cr;
	rsvg_rect box = { 0, 0, ICON, ICON };
	void *handle, *err = NULL;

	if (!lib || !gobj)
		return NULL;
	new_from_file = (rsvg_new_fn)dlsym(lib, "rsvg_handle_new_from_file");
	render = (rsvg_render_fn)dlsym(lib, "rsvg_handle_render_document");
	unref = (unref_fn)dlsym(gobj, "g_object_unref");
	if (!new_from_file || !render || !unref)
		return NULL;
	handle = new_from_file(path, &err);
	if (!handle)
		return NULL;
	out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, ICON, ICON);
	cr = cairo_create(out);
	if (!render(handle, cr, &box, &err)) {
		cairo_destroy(cr);
		cairo_surface_destroy(out);
		unref(handle);
		return NULL;
	}
	cairo_destroy(cr);
	unref(handle);
	return out;
}

static cairo_surface_t *load_png(const char *path)
{
	cairo_surface_t *png = cairo_image_surface_create_from_png(path), *out;
	cairo_t *cr;
	int w, h;
	double k;

	if (cairo_surface_status(png) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(png);
		return NULL;
	}
	w = cairo_image_surface_get_width(png);
	h = cairo_image_surface_get_height(png);
	k = (double)ICON / (w > h ? w : h);
	out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, ICON, ICON);
	cr = cairo_create(out);
	cairo_translate(cr, (ICON - w * k) / 2, (ICON - h * k) / 2);
	cairo_scale(cr, k, k);
	cairo_set_source_surface(cr, png, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
	cairo_paint(cr);
	cairo_destroy(cr);
	cairo_surface_destroy(png);
	return out;
}

static void load_icon(void)
{
	size_t n = strlen(icon_path);

	if (!n)
		return;
	if (n > 4 && !strcmp(icon_path + n - 4, ".svg"))
		icon = load_svg(icon_path);
	else
		icon = load_png(icon_path);
}

/* --- drawing ------------------------------------------------------------- */
static void draw(uint32_t *px)
{
	cairo_surface_t *s = cairo_image_surface_create_for_data(
		(unsigned char *)px, CAIRO_FORMAT_RGB24, width, height, width * 4);
	cairo_t *cr = cairo_create(s);
	PangoLayout *pl = pango_cairo_create_layout(cr);
	PangoFontDescription *fd;
	double cx = width / 2.0, cy, ty;
	int tw, th;

	cairo_set_source_rgb(cr, 0x24 / 255.0, 0x24 / 255.0, 0x24 / 255.0);
	cairo_paint(cr);

	fd = pango_font_description_from_string(icon ? "DejaVu Sans 40" : "DejaVu Sans 52");
	pango_layout_set_font_description(pl, fd);
	pango_font_description_free(fd);
	pango_layout_set_text(pl, label, -1);
	pango_layout_get_pixel_size(pl, &tw, &th);

	if (icon) {
		/* icon, name, ring: about 520 px in all, centred */
		double top = height / 2.0 - 270;

		cairo_set_source_surface(cr, icon, cx - ICON / 2.0, top);
		cairo_paint(cr);
		ty = top + ICON + 18;
		cy = ty + th + 40 + RING;
	} else {
		ty = height / 2.0 - 160;
		cy = height / 2.0 + 70;
	}
	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_move_to(cr, cx - tw / 2.0, ty);
	pango_cairo_show_layout(cr, pl);
	g_object_unref(pl);

	for (int i = 0; i < DOTS; i++) {
		double a = 2 * M_PI * i / DOTS - M_PI / 2;
		int lead = i == step;

		if (lead)
			cairo_set_source_rgb(cr, 0x35 / 255.0, 0x84 / 255.0, 0xe4 / 255.0);
		else
			cairo_set_source_rgb(cr, 0x5e / 255.0, 0x5c / 255.0, 0x64 / 255.0);
		cairo_arc(cr, cx + cos(a) * RING, cy + sin(a) * RING, lead ? 18 : 12, 0, 2 * M_PI);
		cairo_fill(cr);
	}
	cairo_destroy(cr);
	cairo_surface_flush(s);
	cairo_surface_destroy(s);
}

static void buffer_release(void *d, struct wl_buffer *b)
{
	(void)b;
	((struct buffer *)d)->busy = 0;
}

static const struct wl_buffer_listener buffer_listener = { .release = buffer_release };

static void free_buffers(void)
{
	for (int i = 0; i < 2; i++) {
		if (bufs[i].wl) {
			wl_buffer_destroy(bufs[i].wl);
			munmap(bufs[i].data, bufs[i].size);
		}
		memset(&bufs[i], 0, sizeof(bufs[i]));
	}
}

static int make_buffers(void)
{
	size_t size = (size_t)width * height * 4;

	free_buffers();
	for (int i = 0; i < 2; i++) {
		int fd = memfd_create("b8k-splash", MFD_CLOEXEC);
		struct wl_shm_pool *pool;

		if (fd < 0 || ftruncate(fd, (off_t)size) < 0)
			return -1;
		bufs[i].data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		if (bufs[i].data == MAP_FAILED) {
			close(fd);
			return -1;
		}
		bufs[i].size = size;
		pool = wl_shm_create_pool(shm, fd, (int32_t)size);
		bufs[i].wl = wl_shm_pool_create_buffer(pool, 0, width, height, width * 4,
						       WL_SHM_FORMAT_XRGB8888);
		wl_buffer_add_listener(bufs[i].wl, &buffer_listener, &bufs[i]);
		wl_shm_pool_destroy(pool);
		close(fd);
	}
	return 0;
}

static void frame(int full)
{
	struct buffer *b = !bufs[0].busy ? &bufs[0] : (!bufs[1].busy ? &bufs[1] : NULL);

	if (!b || !b->wl)
		return; /* the compositor still holds both: skip a step */
	draw(b->data);
	b->busy = 1;
	wl_surface_attach(surface, b->wl, 0, 0);
	if (full)
		wl_surface_damage(surface, 0, 0, width, height);
	else /* only the ring turns */
		wl_surface_damage(surface, 0, height / 2 - 300, width, 600);
	wl_surface_commit(surface);
}

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
	(void)d;
	zwlr_layer_surface_v1_ack_configure(ls, serial);
	if (w < 100 || h < 100)
		return;
	if ((int)w != width || (int)h != height || !configured) {
		width = (int)w;
		height = (int)h;
		configured = 1;
		if (make_buffers() < 0) {
			running = 0;
			return;
		}
		frame(1);
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

int main(int argc, char **argv)
{
	struct wl_display *display;
	struct itimerspec every = { { 0, STEP_MS * 1000000L }, { 0, STEP_MS * 1000000L } };
	sigset_t mask;
	int sfd, tfd, ticks = 0;

	if (argc > 1 && argv[1][0] && !strchr(argv[1], '/'))
		read_app(argv[1]);

	/* SIGTERM from b8k-launch ends it; block it first so it is never lost. */
	sigemptyset(&mask);
	sigaddset(&mask, SIGTERM);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGHUP);
	sigprocmask(SIG_BLOCK, &mask, NULL);
	sfd = signalfd(-1, &mask, SFD_CLOEXEC);

	display = wl_display_connect(NULL);
	if (!display)
		return 1;
	wl_registry_add_listener(wl_display_get_registry(display), &registry_listener, NULL);
	wl_display_roundtrip(display);
	if (!compositor || !shm || !layer_shell)
		return 1;

	/* Ask for the surface first, then read the icon while it is set up. */
	surface = wl_compositor_create_surface(compositor);
	layer = zwlr_layer_shell_v1_get_layer_surface(layer_shell, surface, NULL,
						      ZWLR_LAYER_SHELL_V1_LAYER_TOP, "b8k-splash");
	zwlr_layer_surface_v1_add_listener(layer, &layer_listener, NULL);
	zwlr_layer_surface_v1_set_size(layer, 0, 0);
	zwlr_layer_surface_v1_set_anchor(layer, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
					 ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
					 ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
					 ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
	/* Zone -1 takes the whole output; the left margin keeps the panel free. */
	zwlr_layer_surface_v1_set_exclusive_zone(layer, -1);
	zwlr_layer_surface_v1_set_margin(layer, 0, 0, 0, BAR);
	wl_surface_commit(surface);
	wl_display_flush(display);
	load_icon();

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
		if (fds[1].revents & POLLIN)
			break;
		if (fds[2].revents & POLLIN) {
			uint64_t n;

			if (read(tfd, &n, sizeof(n)) < 0)
				n = 0;
			if (++ticks >= LIFETIME_STEPS)
				break;
			if (configured) {
				step = (step + 1) % DOTS;
				frame(0);
			}
		}
	}
	zwlr_layer_surface_v1_destroy(layer);
	wl_surface_destroy(surface);
	wl_display_flush(display);
	wl_display_disconnect(display);
	return 0;
}
