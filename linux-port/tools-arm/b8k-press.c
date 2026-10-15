// SPDX-License-Identifier: GPL-2.0
/*
 * b8k-press ID [SIGNAL]: feedback for a tap on the panel.
 *
 * waybar does not apply :active to a touched module, so a tap showed
 * nothing until the program answered. This marks the button as pressed
 * ($XDG_RUNTIME_DIR/b8k-press/ID, which b8k-launch and b8k-meter turn into
 * the "pressed" class), signals waybar to redraw it, buzzes the vibration
 * motor for 70 ms, and clears the mark 300 ms later. It is C because a
 * Python start takes longer than the feedback should.
 *
 * The motor is the regulator-haptic input device (MT6320 VIBR LDO, patch
 * 0039). The waybar signal is SIGRTMIN+SIGNAL, sent only when waybar
 * catches it: an uncaught real-time signal would kill the panel.
 */
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define BUZZ_MS 70  /* shorter is not felt: the motor needs time to spin up */
#define MARK_MS 300

static void sleep_ms(int ms)
{
	struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };

	nanosleep(&ts, NULL);
}

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

/* Signal every waybar of this user that catches SIGRTMIN+sig. */
static void signal_bar(int sig)
{
	int signum = SIGRTMIN + sig;
	DIR *proc;
	struct dirent *de;

	if (sig <= 0 || signum > SIGRTMAX)
		return;
	proc = opendir("/proc");
	if (!proc)
		return;
	while ((de = readdir(proc))) {
		char path[64], line[256];
		FILE *f;
		int catches = 0;

		if (de->d_name[0] < '0' || de->d_name[0] > '9')
			continue;
		snprintf(path, sizeof(path), "/proc/%s/comm", de->d_name);
		if (read_line(path, line, sizeof(line)) || strcmp(line, "waybar"))
			continue;
		snprintf(path, sizeof(path), "/proc/%s/status", de->d_name);
		f = fopen(path, "r");
		if (!f)
			continue;
		while (fgets(line, sizeof(line), f)) {
			if (!strncmp(line, "SigCgt:", 7)) {
				unsigned long long mask = strtoull(line + 7, NULL, 16);

				catches = (mask >> (signum - 1)) & 1;
				break;
			}
		}
		fclose(f);
		if (catches)
			kill((pid_t)atoi(de->d_name), signum);
	}
	closedir(proc);
}

static void buzz(void)
{
	DIR *dir = opendir("/sys/class/input");
	struct dirent *de;
	char node[64] = "";

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

	int fd = open(node, O_RDWR | O_CLOEXEC);
	struct ff_effect effect;
	struct input_event play;

	if (fd < 0)
		return;
	memset(&effect, 0, sizeof(effect));
	effect.type = FF_RUMBLE;
	effect.id = -1;
	effect.u.rumble.strong_magnitude = 0xffff;
	effect.replay.length = BUZZ_MS;
	if (ioctl(fd, EVIOCSFF, &effect) == 0) {
		memset(&play, 0, sizeof(play));
		play.type = EV_FF;
		play.code = (unsigned short)effect.id;
		play.value = 1;
		if (write(fd, &play, sizeof(play)) == (ssize_t)sizeof(play))
			sleep_ms(BUZZ_MS + 5); /* the effect ends when the fd closes */
	}
	close(fd);
}

int main(int argc, char **argv)
{
	const char *run = getenv("XDG_RUNTIME_DIR");
	char dir[256], mark[512];
	struct stat st;
	int sig, fd;

	if (argc < 2 || !argv[1][0] || strchr(argv[1], '/')) {
		fprintf(stderr, "usage: b8k-press ID [SIGNAL]\n");
		return 2;
	}
	sig = argc > 2 ? atoi(argv[2]) : 0;
	/*
	 * The mark lives only in the user's own runtime directory: no /tmp
	 * fallback, and the b8k-press directory must be a real directory of
	 * this user, so nobody can point the mark or its unlink elsewhere.
	 */
	if (!run || run[0] != '/') {
		buzz();
		return 0;
	}
	snprintf(dir, sizeof(dir), "%s/b8k-press", run);
	mkdir(dir, 0700);
	if (lstat(dir, &st) || !S_ISDIR(st.st_mode) || st.st_uid != getuid()) {
		buzz();
		return 0;
	}
	snprintf(mark, sizeof(mark), "%s/%s", dir, argv[1]);

	fd = open(mark, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (fd >= 0)
		close(fd);
	signal_bar(sig);

	/* The child clears the mark later; the caller does not wait for it. */
	if (fork() == 0) {
		setsid();
		sleep_ms(MARK_MS);
		unlink(mark);
		signal_bar(sig);
		_exit(0);
	}
	buzz();
	return 0;
}
