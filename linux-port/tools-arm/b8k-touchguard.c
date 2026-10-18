// SPDX-License-Identifier: GPL-2.0
/*
 * b8k-touchguard: keeps accidental touches off the screen, for a hand that
 * trembles or rests on the glass.
 *
 *   b8k-touchguard [CONFIG]      (default /etc/b8k/touchguard.conf)
 *
 * It grabs the touchscreen (the device with a direct multitouch surface,
 * "Synaptics Blade10" here) and hands the session a filtered copy through
 * uinput, "b8k-touchguard touchscreen", with one finger at a time:
 *
 *  - hold_ms: a touch counts only once it has lasted this long; a brush of
 *    the glass shorter than it is dropped (iOS Hold Duration). The finger
 *    goes down where it first landed (tap assistance), so a slide while it
 *    is held does not move the tap.
 *  - a palm: a contact as large as palm_major, as hard as palm_pressure, or
 *    palm_contacts at once ends the touch in progress as a palm
 *    (MT_TOOL_PALM: libinput cancels it, nothing is tapped) and blocks the
 *    screen until every contact is lifted and palm_grace_ms more.
 *  - repeat_ms / repeat_radius: a new touch this soon after the last one
 *    (from its lift to the new landing), this near to it, is a tremor's
 *    second strike and is dropped (Ignore Repeat); a quick double tap is
 *    one tap. 400 ms and 150 units (some 86 px) by default.
 *  - a second finger while one is down is ignored: one finger at a time.
 *  - buzz_ms: a tap buzzes the motor (the regulator-haptic input device)
 *    this long, 70 ms by default, 0 for none: the shell's tap feedback for
 *    every program alike (ADR 0001), so programs need no buzzing of their
 *    own. A tap is a touch lifted within tap_radius of where it landed; a
 *    finger that moves farther scrolls or drags, and does not buzz. A
 *    finger held still for long_ms buzzes once then (a long press, as
 *    its menu opens) and not again on the lift.
 *
 * Measured on the B8000-H: a finger is size 5 of 15, pressure ~48, 75-120
 * ms; a palm is 12-15, 120-143, with 3-6 contacts and ghost contacts of
 * 9-11 ms around it. The name contains "touch", so b8k-sleep grabs this
 * device too while the screen is off.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define SLOTS 16
#define BITS(n) (((n) + 8 * sizeof(long) - 1) / (8 * sizeof(long)))
#define TEST(bit, arr) ((arr[(bit) / (8 * sizeof(long))] >> ((bit) % (8 * sizeof(long)))) & 1)

enum { FREE, PENDING, LIVE, DEAD };

struct slot {
	int id;  /* tracking id from the screen, -1 when lifted */
	int x, y, x0, y0, major, pressure;
	long long start;
	int state;
	int fresh; /* new this frame: its position events set where it landed */
};

static struct {
	int hold_ms, repeat_ms, repeat_radius, palm_major, palm_pressure, palm_contacts;
	int palm_grace_ms, buzz_ms, tap_radius, long_ms;
} cfg = { 60, 400, 150, 11, 100, 3, 300, 70, 80, 600 };
static int haptic_fd = -1, haptic_effect = -1;
/* The touch handed to the session: when it went down, whether it moved
 * past tap_radius (a scroll: no buzz), whether it has buzzed (long press). */
static long long touch_start;
static int touch_down, touch_moved, touch_buzzed;
static int debug; /* B8K_TOUCHGUARD_DEBUG=1: every decision to stderr */

static struct slot slots[SLOTS];
static int cur_slot, primary = -1, out_id = 1, in_fd = -1, out_fd = -1;
static int palm_block;          /* a palm is (or was just) on the glass */
static long long palm_until;    /* ms: the grace after the palm left */
static long long last_up = -1000000;
static int last_x, last_y;
static volatile sig_atomic_t running = 1;

static long long now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
}

static void logf_(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fputs("b8k-touchguard: ", stderr);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
}

static void read_config(const char *path)
{
	char line[256];
	FILE *f = fopen(path, "r");

	if (!f)
		return;
	while (fgets(line, sizeof(line), f)) {
		char key[64];
		char val[64];

		if (line[0] == '#' || sscanf(line, " %63[a-z_] = %63s", key, val) != 2)
			continue;
		int v = atoi(val);
		if (!strcmp(key, "hold_ms"))
			cfg.hold_ms = v;
		else if (!strcmp(key, "repeat_ms"))
			cfg.repeat_ms = v;
		else if (!strcmp(key, "repeat_radius"))
			cfg.repeat_radius = v;
		else if (!strcmp(key, "palm_major"))
			cfg.palm_major = v;
		else if (!strcmp(key, "palm_pressure"))
			cfg.palm_pressure = v;
		else if (!strcmp(key, "palm_contacts"))
			cfg.palm_contacts = v;
		else if (!strcmp(key, "palm_grace_ms"))
			cfg.palm_grace_ms = v;
		else if (!strcmp(key, "buzz_ms"))
			cfg.buzz_ms = v < 0 ? 0 : (v > 500 ? 500 : v);
		else if (!strcmp(key, "tap_radius"))
			cfg.tap_radius = v;
		else if (!strcmp(key, "long_ms"))
			cfg.long_ms = v;
	}
	fclose(f);
}

/* The first direct multitouch device that is not our own. */
static int open_screen(void)
{
	DIR *d = opendir("/dev/input");
	struct dirent *e;
	int fd = -1;

	if (!d)
		return -1;
	while ((e = readdir(d))) {
		char path[300], name[256] = "";
		unsigned long props[BITS(INPUT_PROP_MAX + 1)] = { 0 };
		unsigned long abs[BITS(ABS_MAX + 1)] = { 0 };

		if (strncmp(e->d_name, "event", 5))
			continue;
		snprintf(path, sizeof(path), "/dev/input/%s", e->d_name);
		int f = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		if (f < 0)
			continue;
		ioctl(f, EVIOCGNAME(sizeof(name)), name);
		ioctl(f, EVIOCGPROP(sizeof(props)), props);
		ioctl(f, EVIOCGBIT(EV_ABS, sizeof(abs)), abs);
		if (strstr(name, "b8k-touchguard") || !TEST(INPUT_PROP_DIRECT, props) ||
		    !TEST(ABS_MT_SLOT, abs) || !TEST(ABS_MT_POSITION_X, abs)) {
			close(f);
			continue;
		}
		logf_("screen %s (%s)", path, name);
		fd = f;
		break;
	}
	closedir(d);
	return fd;
}

static int setup_abs(int code, int min, int max, const struct input_absinfo *from)
{
	struct uinput_abs_setup a = { .code = code };

	if (from)
		a.absinfo = *from;
	else {
		a.absinfo.minimum = min;
		a.absinfo.maximum = max;
	}
	return ioctl(out_fd, UI_ABS_SETUP, &a);
}

static int create_output(void)
{
	struct input_absinfo ai;
	struct uinput_setup us = { 0 };
	static const int copy[] = { ABS_X, ABS_Y, ABS_PRESSURE, ABS_MT_POSITION_X,
				    ABS_MT_POSITION_Y, ABS_MT_TOUCH_MAJOR, ABS_MT_TOUCH_MINOR,
				    ABS_MT_PRESSURE };

	out_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
	if (out_fd < 0)
		return -1;
	ioctl(out_fd, UI_SET_EVBIT, EV_KEY);
	ioctl(out_fd, UI_SET_KEYBIT, BTN_TOUCH);
	ioctl(out_fd, UI_SET_EVBIT, EV_ABS);
	ioctl(out_fd, UI_SET_PROPBIT, INPUT_PROP_DIRECT);
	for (unsigned i = 0; i < sizeof(copy) / sizeof(copy[0]); i++) {
		if (ioctl(in_fd, EVIOCGABS(copy[i]), &ai) < 0)
			continue;
		ioctl(out_fd, UI_SET_ABSBIT, copy[i]);
		setup_abs(copy[i], 0, 0, &ai);
	}
	ioctl(out_fd, UI_SET_ABSBIT, ABS_MT_SLOT);
	setup_abs(ABS_MT_SLOT, 0, 0, NULL); /* one finger: slot 0 only */
	ioctl(out_fd, UI_SET_ABSBIT, ABS_MT_TRACKING_ID);
	setup_abs(ABS_MT_TRACKING_ID, 0, 65535, NULL);
	ioctl(out_fd, UI_SET_ABSBIT, ABS_MT_TOOL_TYPE);
	setup_abs(ABS_MT_TOOL_TYPE, 0, MT_TOOL_MAX, NULL);
	snprintf(us.name, sizeof(us.name), "b8k-touchguard touchscreen");
	us.id.bustype = BUS_VIRTUAL;
	us.id.vendor = 0x06cb;
	if (ioctl(out_fd, UI_DEV_SETUP, &us) < 0 || ioctl(out_fd, UI_DEV_CREATE) < 0)
		return -1;
	return 0;
}

static void emit(int type, int code, int value)
{
	struct input_event ev = { .type = type, .code = code, .value = value };

	if (write(out_fd, &ev, sizeof(ev)) < 0 && errno != EAGAIN)
		logf_("write: %s", strerror(errno));
}

static void sync_out(void)
{
	emit(EV_SYN, SYN_REPORT, 0);
}

/* The motor, opened once with its effect uploaded; a later failure (the
 * device gone) closes it, and the next touch looks for it again. */
static void haptic_open(void)
{
	DIR *dir;
	struct dirent *de;

	if (haptic_fd >= 0 || cfg.buzz_ms <= 0 || !(dir = opendir("/sys/class/input")))
		return;
	while ((de = readdir(dir)) && haptic_fd < 0) {
		char path[300], name[128] = "";
		FILE *f;

		if (strncmp(de->d_name, "event", 5))
			continue;
		snprintf(path, sizeof(path), "/sys/class/input/%s/device/name", de->d_name);
		if (!(f = fopen(path, "r")))
			continue;
		if (!fgets(name, sizeof(name), f))
			name[0] = '\0';
		fclose(f);
		name[strcspn(name, "\n")] = '\0';
		if (strcmp(name, "regulator-haptic"))
			continue;
		snprintf(path, sizeof(path), "/dev/input/%s", de->d_name);
		haptic_fd = open(path, O_RDWR | O_CLOEXEC | O_NONBLOCK);
	}
	closedir(dir);
	if (haptic_fd >= 0) {
		struct ff_effect effect;

		memset(&effect, 0, sizeof(effect));
		effect.type = FF_RUMBLE;
		effect.id = -1;
		effect.u.rumble.strong_magnitude = 0xffff;
		effect.replay.length = (unsigned short)cfg.buzz_ms;
		if (ioctl(haptic_fd, EVIOCSFF, &effect) == 0) {
			haptic_effect = effect.id;
		} else {
			close(haptic_fd);
			haptic_fd = -1;
		}
	}
}

static void buzz(void)
{
	struct input_event play;

	haptic_open();
	if (haptic_fd < 0)
		return;
	memset(&play, 0, sizeof(play));
	play.type = EV_FF;
	play.code = (unsigned short)haptic_effect;
	play.value = 1;
	if (write(haptic_fd, &play, sizeof(play)) != (ssize_t)sizeof(play)) {
		close(haptic_fd);
		haptic_fd = -1;
	}
}

static void out_down(struct slot *s)
{
	emit(EV_ABS, ABS_MT_SLOT, 0);
	emit(EV_ABS, ABS_MT_TRACKING_ID, out_id++ & 0xffff);
	emit(EV_ABS, ABS_MT_TOOL_TYPE, MT_TOOL_FINGER);
	/* Where the finger first landed. */
	emit(EV_ABS, ABS_MT_POSITION_X, s->x0);
	emit(EV_ABS, ABS_MT_POSITION_Y, s->y0);
	emit(EV_ABS, ABS_MT_TOUCH_MAJOR, s->major);
	emit(EV_ABS, ABS_MT_PRESSURE, s->pressure);
	emit(EV_KEY, BTN_TOUCH, 1);
	emit(EV_ABS, ABS_X, s->x0);
	emit(EV_ABS, ABS_Y, s->y0);
	emit(EV_ABS, ABS_PRESSURE, s->pressure);
	sync_out();
	touch_down = 1;
	touch_start = now_ms();
	touch_moved = touch_buzzed = 0;
}

static void out_move(struct slot *s)
{
	long long dx = s->x - s->x0, dy = s->y - s->y0;

	if (dx * dx + dy * dy > (long long)cfg.tap_radius * cfg.tap_radius)
		touch_moved = 1; /* a scroll or a drag: no buzz */
	emit(EV_ABS, ABS_MT_SLOT, 0);
	emit(EV_ABS, ABS_MT_POSITION_X, s->x);
	emit(EV_ABS, ABS_MT_POSITION_Y, s->y);
	emit(EV_ABS, ABS_MT_TOUCH_MAJOR, s->major);
	emit(EV_ABS, ABS_MT_PRESSURE, s->pressure);
	emit(EV_ABS, ABS_X, s->x);
	emit(EV_ABS, ABS_Y, s->y);
	emit(EV_ABS, ABS_PRESSURE, s->pressure);
	sync_out();
}

static void out_up(int as_palm)
{
	emit(EV_ABS, ABS_MT_SLOT, 0);
	if (as_palm) {
		/* libinput cancels a touch whose tool turns into a palm. */
		emit(EV_ABS, ABS_MT_TOOL_TYPE, MT_TOOL_PALM);
		sync_out();
	}
	emit(EV_ABS, ABS_MT_TRACKING_ID, -1);
	emit(EV_KEY, BTN_TOUCH, 0);
	emit(EV_ABS, ABS_PRESSURE, 0);
	sync_out();
	if (touch_down && !as_palm && !touch_moved && !touch_buzzed)
		buzz(); /* a tap, felt as it is lifted (when it is taken) */
	touch_down = 0;
}

/* A finger held still this long: the long press buzzes now, once. */
static void check_long(long long t)
{
	if (touch_down && !touch_moved && !touch_buzzed && t - touch_start >= cfg.long_ms) {
		touch_buzzed = 1;
		buzz();
	}
}

static int is_palm(const struct slot *s)
{
	return s->major >= cfg.palm_major || s->pressure >= cfg.palm_pressure;
}

/* Promote the pending primary once it has been held long enough. */
static void check_hold(long long t)
{
	struct slot *p;

	if (primary < 0)
		return;
	p = &slots[primary];
	if (p->state == PENDING && t - p->start >= cfg.hold_ms) {
		if (debug)
			logf_("touch %d at %d,%d", p->id, p->x0, p->y0);
		p->state = LIVE;
		out_down(p);
	}
}

/* One frame of the screen (up to SYN_REPORT) has been read. */
static void frame(long long t)
{
	int active = 0, palm = 0;

	for (int i = 0; i < SLOTS; i++) {
		if (slots[i].id < 0)
			continue;
		active++;
		if (is_palm(&slots[i]))
			palm = 1;
	}
	if (active >= cfg.palm_contacts)
		palm = 1;
	if (palm && !palm_block) {
		if (debug)
			logf_("palm (%d contacts)", active);
		palm_block = 1;
		if (primary >= 0 && slots[primary].state == LIVE)
			out_up(1);
		primary = -1;
	}
	if (palm_block) {
		for (int i = 0; i < SLOTS; i++)
			if (slots[i].id >= 0)
				slots[i].state = DEAD;
		if (!active) {
			palm_block = 0;
			palm_until = t + cfg.palm_grace_ms;
		}
		return;
	}
	for (int i = 0; i < SLOTS; i++) {
		struct slot *s = &slots[i];

		if (s->id < 0 || s->state != FREE)
			continue;
		/* A new contact. */
		int dx = s->x0 - last_x, dy = s->y0 - last_y;
		int repeat = t - last_up < cfg.repeat_ms &&
			     dx * dx + dy * dy < cfg.repeat_radius * cfg.repeat_radius;

		if (t < palm_until || repeat || primary >= 0) {
			if (debug)
				logf_("drop %d at %d,%d: %s", s->id, s->x0, s->y0,
				      t < palm_until ? "after a palm" : repeat ? "repeat" : "second finger");
			s->state = DEAD;
			continue;
		}
		s->state = PENDING;
		primary = i;
	}
	if (primary >= 0 && slots[primary].state == LIVE)
		out_move(&slots[primary]);
	check_hold(t);
}

static void lifted(int i, long long t)
{
	struct slot *s = &slots[i];

	if (i == primary) {
		if (s->state == LIVE) {
			out_up(0);
			last_up = t;
			last_x = s->x;
			last_y = s->y;
			if (debug)
				logf_("lift %d after %lld ms", s->id, t - s->start);
		} else if (debug) {
			/* A pending one was too short: a brush, dropped. */
			logf_("drop %d: %lld ms, a brush", s->id, t - s->start);
		}
		primary = -1;
	}
	s->state = FREE;
	s->id = -1;
}

static void on_signal(int sig)
{
	(void)sig;
	running = 0;
}

int main(int argc, char **argv)
{
	struct input_event evs[64];

	read_config(argc > 1 ? argv[1] : "/etc/b8k/touchguard.conf");
	haptic_open(); /* found now, not on the first touch */
	debug = getenv("B8K_TOUCHGUARD_DEBUG") != NULL;
	signal(SIGTERM, on_signal);
	signal(SIGINT, on_signal);
	for (int i = 0; i < SLOTS; i++)
		slots[i].id = -1;
	in_fd = open_screen();
	if (in_fd < 0) {
		logf_("no touchscreen");
		return 1;
	}
	if (create_output() < 0) {
		logf_("uinput: %s", strerror(errno));
		return 1;
	}
	/* Let the new device settle before the session loses the old one. */
	usleep(200000);
	if (ioctl(in_fd, EVIOCGRAB, 1) < 0) {
		logf_("grab: %s", strerror(errno));
		return 1;
	}
	logf_("hold %d ms, repeat %d ms / %d, palm size %d pressure %d contacts %d",
	      cfg.hold_ms, cfg.repeat_ms, cfg.repeat_radius, cfg.palm_major, cfg.palm_pressure,
	      cfg.palm_contacts);

	while (running) {
		struct pollfd pf = { in_fd, POLLIN, 0 };
		int timeout = -1;

		check_long(now_ms()); /* a still finger keeps sending events too */

		if (primary >= 0 && slots[primary].state == PENDING) {
			long long left = slots[primary].start + cfg.hold_ms - now_ms();

			timeout = left > 0 ? (int)left : 0;
		} else if (touch_down && !touch_moved && !touch_buzzed) {
			long long left = touch_start + cfg.long_ms - now_ms();

			timeout = left > 0 ? (int)left : 0;
		}
		if (poll(&pf, 1, timeout) < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (pf.revents & (POLLERR | POLLHUP))
			break;
		if (!(pf.revents & POLLIN)) {
			check_hold(now_ms());
			check_long(now_ms());
			continue;
		}
		ssize_t n = read(in_fd, evs, sizeof(evs));
		if (n < 0) {
			if (errno == EAGAIN || errno == EINTR)
				continue;
			break;
		}
		for (int k = 0; k < (int)(n / sizeof(evs[0])); k++) {
			struct input_event *ev = &evs[k];
			long long t = now_ms();
			struct slot *s = &slots[cur_slot];

			if (ev->type == EV_ABS) {
				switch (ev->code) {
				case ABS_MT_SLOT:
					cur_slot = ev->value >= 0 && ev->value < SLOTS ? ev->value : 0;
					break;
				case ABS_MT_TRACKING_ID:
					if (ev->value < 0) {
						if (s->id >= 0)
							lifted(cur_slot, t);
					} else {
						/* The screen sends only what changed: a slot
						 * used again keeps its last position. */
						s->id = ev->value;
						s->start = t;
						s->x0 = s->x;
						s->y0 = s->y;
						s->fresh = 1;
						s->state = FREE;
					}
					break;
				case ABS_MT_POSITION_X:
					s->x = ev->value;
					if (s->fresh)
						s->x0 = ev->value;
					break;
				case ABS_MT_POSITION_Y:
					s->y = ev->value;
					if (s->fresh)
						s->y0 = ev->value;
					break;
				case ABS_MT_TOUCH_MAJOR:
					s->major = ev->value;
					break;
				case ABS_MT_PRESSURE:
					s->pressure = ev->value;
					break;
				}
			} else if (ev->type == EV_SYN && ev->code == SYN_REPORT) {
				frame(t);
				for (int i = 0; i < SLOTS; i++)
					slots[i].fresh = 0;
			} else if (ev->type == EV_SYN && ev->code == SYN_DROPPED) {
				/* Lost events: start clean. */
				if (primary >= 0 && slots[primary].state == LIVE)
					out_up(1);
				primary = -1;
				for (int i = 0; i < SLOTS; i++) {
					slots[i].id = -1;
					slots[i].state = FREE;
				}
			}
		}
	}
	if (primary >= 0 && slots[primary].state == LIVE)
		out_up(1);
	ioctl(in_fd, EVIOCGRAB, 0);
	ioctl(out_fd, UI_DEV_DESTROY);
	return 0;
}
