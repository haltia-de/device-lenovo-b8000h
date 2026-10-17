// SPDX-License-Identifier: GPL-2.0
/*
 * b8k-home: the home screen, every program as a large tile, with the time,
 * the date and the weather above them.
 *
 *   b8k-home            run it (b8k-launcher starts one per session)
 *   b8k-home toggle     raise it over the windows, or lower it again
 *   b8k-home show|hide  raise it, lower it
 *   b8k-home reload     read apps.conf again
 *
 * It lives on the background layer right of the panel, so it is what one
 * sees with no window open. The panel's home button raises it to the top
 * layer over the windows; a tap on a tile starts or brings up that program
 * (b8k-launch start) and lowers it again. A long press asks whether to put
 * the program on the panel or take it off (b8k-ask pin). While it is up,
 * $XDG_RUNTIME_DIR/b8k-home.raised exists (b8k-launch lowers it before a
 * panel tap, b8k-meter lights the home button) and the panel's home button
 * is signalled (SIGRTMIN+8) on every change.
 *
 * The tiles are the programs of the session's apps.conf (b8k-apps), in its
 * order, without the ones marked "home = no". An unread count a program
 * writes to $XDG_RUNTIME_DIR/b8k-badge/<id> shows on its tile. The weather
 * comes from the cache of the weather program (~/.cache/b8k-weather.json,
 * "now": "t" and "descr") while it is under three hours old; a tap on it
 * opens that program, a tap on the time and date opens the calendar. Icons as in b8k-splash: PNG through cairo, SVG
 * through librsvg opened with dlopen.
 */
#define _GNU_SOURCE
#include <cairo.h>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <pango/pangocairo.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#define APPS "/usr/local/share/b8k/apps.conf"
#define MAX_APPS 48
#define ICON 96
#define TILE_W 200
#define TILE_H 176
#define GAP 16
#define PAD 32
#define HEADER 170
#define LONG_MS 600
#define SLOP 24 /* px a finger may move and still tap */
#define BADGE_POLL_S 5
#define WEATHER_MAX_AGE (3 * 3600)
#define WEATHER_ID "weather"
#define CALENDAR_ID "calendar"

extern char **environ;

struct app {
	char id[64];
	char label[128];
	char icon_path[512];
	cairo_surface_t *icon;
	char badge[16];
};

static struct app apps[MAX_APPS];
static int napps;
static time_t apps_mtime;

static struct wl_display *display;
static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct wl_seat *seat;
static struct zwlr_layer_shell_v1 *layer_shell;
static uint32_t layer_version;
static struct wl_surface *surface;
static struct zwlr_layer_surface_v1 *layer;
static struct wl_touch *touch;
static struct wl_pointer *pointer;

static int width, height, configured, running = 1, raised, dirty;
static int scroll, scroll_max;
static int press = -1, press_x, press_y, press_scroll, moved, held;
static int32_t touch_id = -1;
static int ptr_x, ptr_y, ptr_down;
static int long_fd;
static char weather[2][64];
static char shown_weather[2][64];
static time_t weather_mtime, weather_fetched;
static char apps_path[512];

struct buffer {
	struct wl_buffer *wl;
	void *data;
	size_t size;
	int busy;
};
static struct buffer bufs[2];

/* --- small helpers --------------------------------------------------------- */
static void trim(char *s)
{
	char *e = s + strlen(s);

	while (e > s && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t'))
		*--e = '\0';
}

static const char *runtime_dir(void)
{
	const char *run = getenv("XDG_RUNTIME_DIR");

	return run && run[0] == '/' ? run : NULL;
}

/* A program started from here gets a clean signal mask and default
 * handlers: ours blocks SIGTERM and others for the signalfd, and a child
 * that inherited that could not be stopped with SIGTERM. */
static void spawn(const char *a0, const char *a1, const char *a2)
{
	char *argv[] = { (char *)a0, (char *)a1, (char *)a2, NULL };
	posix_spawnattr_t attr;
	sigset_t none, all;
	pid_t pid;

	sigemptyset(&none);
	sigfillset(&all);
	posix_spawnattr_init(&attr);
	posix_spawnattr_setsigmask(&attr, &none);
	posix_spawnattr_setsigdefault(&attr, &all);
	posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);
	posix_spawn(&pid, a0, NULL, &attr, argv, environ);
	posix_spawnattr_destroy(&attr);
}

/* --- icons ---------------------------------------------------------------- */
typedef struct {
	double x, y, width, height;
} rsvg_rect;
typedef void *(*rsvg_new_fn)(const char *, void **);
typedef int (*rsvg_render_fn)(void *, cairo_t *, const rsvg_rect *, void **);
typedef void (*unref_fn)(void *);

static cairo_surface_t *load_svg(const char *path)
{
	static void *lib, *gobj;
	rsvg_new_fn new_from_file;
	rsvg_render_fn render;
	unref_fn unref;
	cairo_surface_t *out;
	cairo_t *cr;
	rsvg_rect box = { 0, 0, ICON, ICON };
	void *handle, *err = NULL;
	int ok;

	if (!lib)
		lib = dlopen("librsvg-2.so.2", RTLD_NOW | RTLD_LOCAL);
	if (!gobj)
		gobj = dlopen("libgobject-2.0.so.0", RTLD_NOW | RTLD_LOCAL);
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
	ok = render(handle, cr, &box, &err);
	cairo_destroy(cr);
	unref(handle);
	if (!ok) {
		cairo_surface_destroy(out);
		return NULL;
	}
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

static cairo_surface_t *load_icon(const char *path)
{
	size_t n = strlen(path);

	if (!n)
		return NULL;
	if (n > 4 && !strcmp(path + n - 4, ".svg"))
		return load_svg(path);
	return load_png(path);
}

/* --- apps.conf, badges, weather ------------------------------------------- */
static void pick_apps_path(void)
{
	const char *run = runtime_dir();
	struct stat st;

	/* The session's copy written by b8k-apps, else the built-in one. */
	if (run) {
		snprintf(apps_path, sizeof(apps_path), "%s/b8k/apps.conf", run);
		if (!stat(apps_path, &st))
			return;
	}
	snprintf(apps_path, sizeof(apps_path), "%s", APPS);
}

static void read_apps(void)
{
	char line[640];
	struct app next[MAX_APPS], cur;
	int n = 0, open_ = 0, skip = 0, runnable = 0;
	struct stat st;
	FILE *f;

	pick_apps_path();
	if (stat(apps_path, &st) < 0 || !(f = fopen(apps_path, "r")))
		return;
	apps_mtime = st.st_mtime;
	memset(next, 0, sizeof(next));
	memset(&cur, 0, sizeof(cur));
	for (;;) {
		char *eq, *val, *key = line;
		int eof = !fgets(line, sizeof(line), f);

		if (!eof) {
			trim(line);
			while (*key == ' ' || *key == '\t')
				key++;
			if (*key == '#' || *key == ';' || !*key)
				continue;
		}
		if (eof || *key == '[') {
			/* Close the section: a program for the home screen has
			 * something to run and is not marked "home = no". */
			if (open_ && runnable && !skip && n < MAX_APPS)
				next[n++] = cur;
			if (eof)
				break;
			char *end = strchr(key, ']');

			open_ = end != NULL;
			if (!open_)
				continue;
			*end = '\0';
			memset(&cur, 0, sizeof(cur));
			snprintf(cur.id, sizeof(cur.id), "%s", key + 1);
			snprintf(cur.label, sizeof(cur.label), "%s", key + 1);
			skip = 0;
			runnable = 0;
			continue;
		}
		if (!open_ || !(eq = strchr(key, '=')))
			continue;
		val = eq + 1;
		while (*val == ' ' || *val == '\t')
			val++;
		*eq = '\0';
		trim(key);
		if (!strcmp(key, "label"))
			snprintf(cur.label, sizeof(cur.label), "%s", val);
		else if (!strcmp(key, "icon"))
			snprintf(cur.icon_path, sizeof(cur.icon_path), "%s", val);
		else if (!strcmp(key, "exec") || !strcmp(key, "url"))
			runnable = 1;
		else if (!strcmp(key, "home") && !strcmp(val, "no"))
			skip = 1;
	}
	fclose(f);

	/* Keep the icons already drawn; load the new ones. */
	for (int i = 0; i < n; i++) {
		for (int j = 0; j < napps; j++) {
			if (apps[j].icon && !strcmp(apps[j].icon_path, next[i].icon_path)) {
				next[i].icon = apps[j].icon;
				apps[j].icon = NULL;
				break;
			}
		}
		if (!next[i].icon)
			next[i].icon = load_icon(next[i].icon_path);
	}
	for (int j = 0; j < napps; j++)
		if (apps[j].icon)
			cairo_surface_destroy(apps[j].icon);
	memcpy(apps, next, sizeof(apps));
	napps = n;
	dirty = 1;
}

/* A JSON string at p (after its opening quote) into UTF-8; \\uXXXX of the
 * Basic Multilingual Plane is enough for Russian words. */
static void json_string(const char *p, char *out, size_t n)
{
	size_t o = 0;

	while (*p && *p != '"' && o + 4 < n) {
		if (*p == '\\' && p[1] == 'u') {
			unsigned c = (unsigned)strtoul((char[5]){ p[2], p[3], p[4], p[5], 0 }, NULL, 16);

			if (c < 0x80) {
				out[o++] = (char)c;
			} else if (c < 0x800) {
				out[o++] = (char)(0xc0 | c >> 6);
				out[o++] = (char)(0x80 | (c & 0x3f));
			} else {
				out[o++] = (char)(0xe0 | c >> 12);
				out[o++] = (char)(0x80 | (c >> 6 & 0x3f));
				out[o++] = (char)(0x80 | (c & 0x3f));
			}
			p += 6;
		} else if (*p == '\\' && p[1]) {
			out[o++] = p[1];
			p += 2;
		} else {
			out[o++] = *p++;
		}
	}
	out[o] = '\0';
}

/* The temperature and the words of "now" in the weather program's cache. */
static void read_weather(char w[2][64])
{
	char path[512], *data, *now, *k;
	const char *home = getenv("HOME");
	struct stat st;
	FILE *f;
	size_t len;

	w[0][0] = w[1][0] = '\0';
	if (!home)
		return;
	snprintf(path, sizeof(path), "%s/.cache/b8k-weather.json", home);
	if (stat(path, &st) < 0 || st.st_size > 1 << 20)
		return;
	if (st.st_mtime != weather_mtime) {
		weather_mtime = st.st_mtime;
		weather_fetched = 0;
		static char t[64], d[64];

		t[0] = d[0] = '\0';
		if (!(f = fopen(path, "r")))
			return;
		data = calloc(1, (size_t)st.st_size + 1);
		len = data ? fread(data, 1, (size_t)st.st_size, f) : 0;
		fclose(f);
		if (!data)
			return;
		data[len] = '\0';
		if ((k = strstr(data, "\"fetched\":")))
			weather_fetched = (time_t)strtod(k + 10, NULL);
		if ((now = strstr(data, "\"now\":"))) {
			char *end = strchr(now, '}');

			if (end)
				*end = '\0';
			if ((k = strstr(now, "\"t\":"))) {
				long v = lround(strtod(k + 4, NULL));

				snprintf(t, sizeof(t), "%s%ld°", v > 0 ? "+" : (v < 0 ? "−" : ""),
					 labs(v));
			}
			if ((k = strstr(now, "\"descr\":")) && (k = strchr(k + 8, '"')))
				json_string(k + 1, d, sizeof(d));
		}
		free(data);
		snprintf(w[0], 64, "%s", t);
		snprintf(w[1], 64, "%s", d);
		memcpy(weather, w, sizeof(weather));
	}
	if (time(NULL) - weather_fetched > WEATHER_MAX_AGE)
		w[0][0] = w[1][0] = '\0';
	else
		memcpy(w, weather, sizeof(weather));
}

/* Re-read what others write: unread counts, the weather, apps.conf. */
static void poll_files(void)
{
	const char *run = runtime_dir();
	char path[640], buf[64];
	struct stat st;
	FILE *f;

	if (!stat(apps_path, &st) && st.st_mtime != apps_mtime)
		read_apps();
	if (!run)
		return;
	for (int i = 0; i < napps; i++) {
		buf[0] = '\0';
		snprintf(path, sizeof(path), "%s/b8k-badge/%s", run, apps[i].id);
		if ((f = fopen(path, "r"))) {
			if (!fgets(buf, sizeof(buf), f))
				buf[0] = '\0';
			fclose(f);
			trim(buf);
			if (!strcmp(buf, "0") || strspn(buf, "0123456789") != strlen(buf))
				buf[0] = '\0';
		}
		if (strcmp(buf, apps[i].badge)) {
			snprintf(apps[i].badge, sizeof(apps[i].badge), "%s", buf);
			dirty = 1;
		}
	}
	char w[2][64];
	read_weather(w);
	if (memcmp(w, shown_weather, sizeof(w))) {
		memcpy(shown_weather, w, sizeof(w));
		dirty = 1;
	}
}

/* --- drawing -------------------------------------------------------------- */
static void rgb(cairo_t *cr, uint32_t c)
{
	cairo_set_source_rgb(cr, (c >> 16 & 255) / 255.0, (c >> 8 & 255) / 255.0,
			     (c & 255) / 255.0);
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

static void text(cairo_t *cr, PangoLayout *pl, const char *font, const char *s, double x,
		 double y, int align, int max_w)
{
	PangoFontDescription *fd = pango_font_description_from_string(font);
	int tw, th;

	pango_layout_set_font_description(pl, fd);
	pango_font_description_free(fd);
	pango_layout_set_width(pl, max_w > 0 ? max_w * PANGO_SCALE : -1);
	pango_layout_set_ellipsize(pl, max_w > 0 ? PANGO_ELLIPSIZE_END : PANGO_ELLIPSIZE_NONE);
	pango_layout_set_text(pl, s, -1);
	pango_layout_get_pixel_size(pl, &tw, &th);
	if (align == 0) /* centre */
		x -= tw / 2.0;
	else if (align > 0) /* right */
		x -= tw;
	cairo_move_to(cr, x, y);
	pango_cairo_show_layout(cr, pl);
}

static int columns(void)
{
	int c = (width - 2 * PAD + GAP) / (TILE_W + GAP);

	return c < 1 ? 1 : c;
}

static void tile_rect(int i, int *x, int *y)
{
	int c = columns();
	int left = (width - (c * TILE_W + (c - 1) * GAP)) / 2;

	*x = left + (i % c) * (TILE_W + GAP);
	*y = HEADER + (i / c) * (TILE_H + GAP) - scroll;
}

static int tile_at(int px, int py)
{
	for (int i = 0; i < napps; i++) {
		int x, y;

		tile_rect(i, &x, &y);
		if (py < HEADER)
			return -1;
		if (px >= x && px < x + TILE_W && py >= y && py < y + TILE_H)
			return i;
	}
	return -1;
}

static void draw(uint32_t *px)
{
	static const char *days[] = { "воскресенье", "понедельник", "вторник", "среда",
				      "четверг", "пятница", "суббота" };
	static const char *months[] = { "января", "февраля", "марта", "апреля", "мая",
					"июня", "июля", "августа", "сентября", "октября",
					"ноября", "декабря" };
	cairo_surface_t *s = cairo_image_surface_create_for_data(
		(unsigned char *)px, CAIRO_FORMAT_RGB24, width, height, width * 4);
	cairo_t *cr = cairo_create(s);
	PangoLayout *pl = pango_cairo_create_layout(cr);
	time_t now = time(NULL);
	struct tm tm;
	char buf[96];
	int rows, c = columns();

	localtime_r(&now, &tm);
	rows = (napps + c - 1) / c;
	scroll_max = HEADER + rows * (TILE_H + GAP) + PAD - height;
	if (scroll_max < 0)
		scroll_max = 0;
	if (scroll > scroll_max)
		scroll = scroll_max;

	rgb(cr, 0x242424);
	cairo_paint(cr);

	/* Tiles first, so the header stays over them when they scroll. */
	for (int i = 0; i < napps; i++) {
		struct app *a = &apps[i];
		int x, y;

		tile_rect(i, &x, &y);
		if (y + TILE_H < HEADER || y > height)
			continue;
		rounded(cr, x, y, TILE_W, TILE_H, 16);
		rgb(cr, i == press && !moved ? 0x3584e4 : 0x303030);
		cairo_fill(cr);
		if (a->icon) {
			cairo_set_source_surface(cr, a->icon, x + (TILE_W - ICON) / 2.0, y + 18);
			cairo_paint(cr);
		}
		rgb(cr, 0xffffff);
		text(cr, pl, "DejaVu Sans 19", a->label, x + TILE_W / 2.0, y + ICON + 30, 0,
		     TILE_W - 16);
		if (a->badge[0]) {
			double bx = x + TILE_W / 2.0 + ICON / 2.0, by = y + 22;

			cairo_arc(cr, bx, by, 20, 0, 2 * M_PI);
			rgb(cr, 0xe01b24);
			cairo_fill(cr);
			rgb(cr, 0xffffff);
			text(cr, pl, "DejaVu Sans Bold 17", a->badge, bx, by - 13, 0, 0);
		}
	}

	rgb(cr, 0x242424);
	cairo_rectangle(cr, 0, 0, width, HEADER - GAP);
	cairo_fill(cr);
	rgb(cr, 0xffffff);
	snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
	text(cr, pl, "DejaVu Sans Bold 64", buf, PAD + 8, 16, -1, 0);
	snprintf(buf, sizeof(buf), "%s, %d %s", days[tm.tm_wday], tm.tm_mday,
		 months[tm.tm_mon]);
	rgb(cr, 0xc0bfbc);
	text(cr, pl, "DejaVu Sans 24", buf, PAD + 12, 108, -1, 0);
	if (shown_weather[0][0]) {
		rgb(cr, 0xffffff);
		text(cr, pl, "DejaVu Sans Bold 52", shown_weather[0], width - PAD - 8, 24, 1, 0);
		rgb(cr, 0xc0bfbc);
		text(cr, pl, "DejaVu Sans 24", shown_weather[1], width - PAD - 12, 108, 1,
		     width / 2);
	}

	g_object_unref(pl);
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
		int fd = memfd_create("b8k-home", MFD_CLOEXEC);
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

static void redraw(void)
{
	struct buffer *b = !bufs[0].busy ? &bufs[0] : (!bufs[1].busy ? &bufs[1] : NULL);

	if (!configured || !b || !b->wl)
		return; /* both still held: dirty stays set, the next event retries */
	draw(b->data);
	b->busy = 1;
	dirty = 0;
	wl_surface_attach(surface, b->wl, 0, 0);
	wl_surface_damage(surface, 0, 0, width, height);
	wl_surface_commit(surface);
}

/* --- raising and lowering ------------------------------------------------- */
#define HOME_SIGNAL 8

/* Tell the panel's home button, if the bar already catches its signal (an
 * uncaught real-time signal would end waybar and the session with it). */
static void signal_panel(void)
{
	int signum = SIGRTMIN + HOME_SIGNAL;
	DIR *proc = opendir("/proc");
	struct dirent *e;

	if (!proc)
		return;
	while ((e = readdir(proc))) {
		char path[64], line[256];
		unsigned long long caught = 0;
		int is_bar = 0;
		FILE *f;

		if (e->d_name[0] < '1' || e->d_name[0] > '9')
			continue;
		snprintf(path, sizeof(path), "/proc/%s/status", e->d_name);
		if (!(f = fopen(path, "r")))
			continue;
		while (fgets(line, sizeof(line), f)) {
			if (!strncmp(line, "Name:", 5))
				is_bar = !strcmp(line + 5 + strspn(line + 5, " \t"), "waybar\n");
			else if (!strncmp(line, "SigCgt:", 7))
				caught = strtoull(line + 7, NULL, 16);
		}
		fclose(f);
		if (is_bar && (caught >> (signum - 1) & 1))
			kill((pid_t)atoi(e->d_name), signum);
	}
	closedir(proc);
}

/* The close button (b8k-close) hides while the home screen is up. */
static void signal_close_button(void)
{
	const char *run = runtime_dir();
	char path[512], buf[32];
	FILE *f;

	if (!run)
		return;
	snprintf(path, sizeof(path), "%s/b8k-close.pid", run);
	if (!(f = fopen(path, "r")))
		return;
	if (fgets(buf, sizeof(buf), f) && atoi(buf) > 1)
		kill((pid_t)atoi(buf), SIGUSR1);
	fclose(f);
}

/* A program starting under its curtain (b8k-launch writes "id pid
 * curtain-pid"): the home screen takes the curtain away; the program goes
 * on starting and its window comes up under the home screen. */
static void drop_curtain(void)
{
	const char *run = runtime_dir();
	char path[512], id[128];
	int pid = 0, curtain = 0;
	FILE *f;

	if (!run)
		return;
	snprintf(path, sizeof(path), "%s/b8k-launch/starting", run);
	if (!(f = fopen(path, "r")))
		return;
	if (fscanf(f, "%127s %d %d", id, &pid, &curtain) == 3 && curtain > 1)
		kill((pid_t)curtain, SIGTERM);
	fclose(f);
}

static void set_raised(int up)
{
	const char *run = runtime_dir();
	char path[512];

	if (up == raised || layer_version < 2)
		return;
	raised = up;
	if (run) {
		snprintf(path, sizeof(path), "%s/b8k-home.raised", run);
		if (up)
			close(open(path, O_WRONLY | O_CREAT | O_CLOEXEC, 0600));
		else
			unlink(path);
	}
	signal_panel();
	signal_close_button();
	if (up)
		drop_curtain();
	scroll = 0;
	zwlr_layer_surface_v1_set_layer(layer, up ? ZWLR_LAYER_SHELL_V1_LAYER_TOP
						 : ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND);
	dirty = 1;
	wl_surface_commit(surface);
}

/* --- input ---------------------------------------------------------------- */
static void arm_long_press(int on)
{
	struct itimerspec t = { { 0, 0 }, { on ? LONG_MS / 1000 : 0,
					    on ? (LONG_MS % 1000) * 1000000L : 0 } };

	timerfd_settime(long_fd, 0, &t, NULL);
}

static void press_down(int x, int y)
{
	press = tile_at(x, y);
	/* The weather in the header opens the weather program. */
	if (press < 0 && y < HEADER && x > width / 2 && shown_weather[0][0])
		press = -2;
	/* The time and the date open the calendar. */
	if (press < 0 && y < HEADER && x <= width / 2)
		press = -3;
	press_x = x;
	press_y = y;
	press_scroll = scroll;
	moved = 0;
	held = 0;
	arm_long_press(press >= 0);
	dirty = 1;
}

static void press_move(int x, int y)
{
	if (abs(y - press_y) > SLOP || abs(x - press_x) > SLOP) {
		if (!moved)
			arm_long_press(0);
		moved = 1;
	}
	if (moved && scroll_max > 0) {
		int s = press_scroll - (y - press_y);

		scroll = s < 0 ? 0 : (s > scroll_max ? scroll_max : s);
	}
	dirty = 1;
}

static void press_up(void)
{
	arm_long_press(0);
	if ((press == -2 || press == -3) && !moved) {
		const char *id = press == -2 ? WEATHER_ID : CALENDAR_ID;

		spawn("/usr/local/bin/b8k-press", id, NULL);
		spawn("/usr/local/bin/b8k-launch", "start", id);
		set_raised(0);
	} else if (press >= 0 && !moved && !held) {
		spawn("/usr/local/bin/b8k-press", apps[press].id, NULL);
		spawn("/usr/local/bin/b8k-launch", "start", apps[press].id);
		set_raised(0);
	}
	press = -1;
	dirty = 1;
}

static void long_press(void)
{
	if (press < 0 || moved)
		return;
	held = 1;
	spawn("/usr/local/bin/b8k-ask", "pin", apps[press].id);
	press = -1;
	dirty = 1;
}

static void touch_down(void *d, struct wl_touch *t, uint32_t serial, uint32_t time,
		       struct wl_surface *s, int32_t id, wl_fixed_t x, wl_fixed_t y)
{
	(void)d; (void)t; (void)serial; (void)time; (void)s;
	if (touch_id >= 0)
		return; /* one finger at a time */
	touch_id = id;
	press_down(wl_fixed_to_int(x), wl_fixed_to_int(y));
}

static void touch_up(void *d, struct wl_touch *t, uint32_t serial, uint32_t time, int32_t id)
{
	(void)d; (void)t; (void)serial; (void)time;
	if (id != touch_id)
		return;
	touch_id = -1;
	press_up();
}

static void touch_motion(void *d, struct wl_touch *t, uint32_t time, int32_t id, wl_fixed_t x,
			 wl_fixed_t y)
{
	(void)d; (void)t; (void)time;
	if (id == touch_id)
		press_move(wl_fixed_to_int(x), wl_fixed_to_int(y));
}

static void touch_frame(void *d, struct wl_touch *t)
{
	(void)d; (void)t;
}

static void touch_cancel(void *d, struct wl_touch *t)
{
	(void)d; (void)t;
	touch_id = -1;
	press = -1;
	arm_long_press(0);
	dirty = 1;
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
	(void)d; (void)p; (void)serial; (void)s;
	ptr_x = wl_fixed_to_int(x);
	ptr_y = wl_fixed_to_int(y);
}

static void ptr_leave(void *d, struct wl_pointer *p, uint32_t serial, struct wl_surface *s)
{
	(void)d; (void)p; (void)serial; (void)s;
	if (ptr_down) {
		ptr_down = 0;
		press = -1;
		arm_long_press(0);
		dirty = 1;
	}
}

static void ptr_motion(void *d, struct wl_pointer *p, uint32_t time, wl_fixed_t x, wl_fixed_t y)
{
	(void)d; (void)p; (void)time;
	ptr_x = wl_fixed_to_int(x);
	ptr_y = wl_fixed_to_int(y);
	if (ptr_down)
		press_move(ptr_x, ptr_y);
}

static void ptr_button(void *d, struct wl_pointer *p, uint32_t serial, uint32_t time,
		       uint32_t button, uint32_t state)
{
	(void)d; (void)p; (void)serial; (void)time;
	if (button != 0x110) /* BTN_LEFT */
		return;
	if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
		ptr_down = 1;
		press_down(ptr_x, ptr_y);
	} else if (ptr_down) {
		ptr_down = 0;
		press_up();
	}
}

static void ptr_axis(void *d, struct wl_pointer *p, uint32_t time, uint32_t axis, wl_fixed_t v)
{
	(void)d; (void)p; (void)time;
	if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL && scroll_max > 0) {
		scroll += wl_fixed_to_int(v) * 4;
		scroll = scroll < 0 ? 0 : (scroll > scroll_max ? scroll_max : scroll);
		dirty = 1;
	}
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
	else if (!strcmp(iface, wl_seat_interface.name) && !seat) {
		seat = wl_registry_bind(r, name, &wl_seat_interface, version < 5 ? version : 5);
		wl_seat_add_listener(seat, &seat_listener, NULL);
	} else if (!strcmp(iface, zwlr_layer_shell_v1_interface.name)) {
		layer_version = version < 4 ? version : 4;
		layer_shell = wl_registry_bind(r, name, &zwlr_layer_shell_v1_interface,
					       layer_version);
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
	}
	dirty = 1;
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

/* --- the command form: signal the running one ------------------------------ */
static int pid_path(char *out, size_t n)
{
	const char *run = runtime_dir();

	if (!run)
		return -1;
	snprintf(out, n, "%s/b8k-home.pid", run);
	return 0;
}

static int command(const char *cmd)
{
	char path[512], buf[32];
	int sig;
	pid_t pid;
	FILE *f;

	if (!strcmp(cmd, "toggle"))
		sig = SIGUSR1;
	else if (!strcmp(cmd, "show"))
		sig = SIGRTMIN + 1;
	else if (!strcmp(cmd, "hide"))
		sig = SIGUSR2;
	else if (!strcmp(cmd, "reload"))
		sig = SIGHUP;
	else {
		fprintf(stderr, "usage: b8k-home [toggle|show|hide|reload]\n");
		return 2;
	}
	if (pid_path(path, sizeof(path)) < 0 || !(f = fopen(path, "r")))
		return 1;
	if (!fgets(buf, sizeof(buf), f)) {
		fclose(f);
		return 1;
	}
	fclose(f);
	pid = (pid_t)atoi(buf);
	return pid > 1 && kill(pid, sig) == 0 ? 0 : 1;
}

static void arm_minute(int tfd)
{
	/* Fire at the start of every minute, so the clock turns on time. */
	struct timespec now;
	struct itimerspec t = { { 60, 0 }, { 0, 0 } };

	clock_gettime(CLOCK_REALTIME, &now);
	t.it_value.tv_sec = now.tv_sec - now.tv_sec % 60 + 60;
	timerfd_settime(tfd, TFD_TIMER_ABSTIME, &t, NULL);
}

int main(int argc, char **argv)
{
	struct itimerspec poll_every = { { BADGE_POLL_S, 0 }, { BADGE_POLL_S, 0 } };
	char path[512], buf[32];
	sigset_t mask;
	int sfd, minute_fd, poll_fd, lock;

	if (argc > 1)
		return command(argv[1]);

	/* One per session: the pid file is also the lock. */
	if (pid_path(path, sizeof(path)) < 0)
		return 1;
	lock = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
	if (lock < 0 || flock(lock, LOCK_EX | LOCK_NB) < 0)
		return 0;
	if (ftruncate(lock, 0) == 0) {
		snprintf(buf, sizeof(buf), "%d\n", getpid());
		if (write(lock, buf, strlen(buf)) < 0)
			perror("b8k-home: pid file");
	}

	signal(SIGCHLD, SIG_IGN); /* the programs started are not waited for */
	sigemptyset(&mask);
	sigaddset(&mask, SIGTERM);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGHUP);
	sigaddset(&mask, SIGUSR1);
	sigaddset(&mask, SIGUSR2);
	sigaddset(&mask, SIGRTMIN + 1);
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
						      ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
						      "b8k-home");
	zwlr_layer_surface_v1_add_listener(layer, &layer_listener, NULL);
	zwlr_layer_surface_v1_set_size(layer, 0, 0);
	zwlr_layer_surface_v1_set_anchor(layer, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
					 ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
					 ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
					 ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
	/* Zone 0: the space the panel (and the keyboard) leave. */
	zwlr_layer_surface_v1_set_exclusive_zone(layer, 0);
	wl_surface_commit(surface);
	wl_display_flush(display);
	read_apps();
	poll_files();

	minute_fd = timerfd_create(CLOCK_REALTIME, TFD_CLOEXEC);
	arm_minute(minute_fd);
	poll_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
	timerfd_settime(poll_fd, 0, &poll_every, NULL);
	long_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);

	while (running) {
		struct pollfd fds[5] = {
			{ wl_display_get_fd(display), POLLIN, 0 },
			{ sfd, POLLIN, 0 },
			{ minute_fd, POLLIN, 0 },
			{ poll_fd, POLLIN, 0 },
			{ long_fd, POLLIN, 0 },
		};
		uint64_t n;

		if (dirty)
			redraw();
		while (wl_display_prepare_read(display) != 0)
			wl_display_dispatch_pending(display);
		wl_display_flush(display);
		if (poll(fds, 5, -1) < 0 && errno != EINTR) {
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

			if (read(sfd, &si, sizeof(si)) == sizeof(si)) {
				if (si.ssi_signo == SIGUSR1)
					set_raised(!raised);
				else if (si.ssi_signo == SIGUSR2)
					set_raised(0);
				else if ((int)si.ssi_signo == SIGRTMIN + 1)
					set_raised(1);
				else if (si.ssi_signo == SIGHUP)
					read_apps();
				else
					running = 0;
			}
		}
		if (fds[2].revents & POLLIN) {
			if (read(minute_fd, &n, sizeof(n)) < 0)
				n = 0;
			arm_minute(minute_fd); /* also after a clock change or sleep */
			dirty = 1;
		}
		if (fds[3].revents & POLLIN) {
			if (read(poll_fd, &n, sizeof(n)) < 0)
				n = 0;
			poll_files();
		}
		if (fds[4].revents & POLLIN) {
			if (read(long_fd, &n, sizeof(n)) < 0)
				n = 0;
			long_press();
		}
	}
	set_raised(0);
	zwlr_layer_surface_v1_destroy(layer);
	wl_surface_destroy(surface);
	wl_display_flush(display);
	wl_display_disconnect(display);
	unlink(path);
	return 0;
}
