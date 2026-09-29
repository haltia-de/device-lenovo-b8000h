/*
 * b8k-session: run a command inside a PAM session so pam_elogind registers
 * a logind session for it (Phosh needs one: brightness goes through
 * org.freedesktop.login1.Session.SetBrightness, which fails without it).
 *
 *   b8k-session [-u user] <command> [args...]
 *
 * PAM service: /etc/pam.d/b8k-session (auth/account pam_permit, session
 * base-session).  Runs as root; no password prompt.  With -u the session
 * belongs to that user and the command runs with the user's uid, groups,
 * HOME and XDG_RUNTIME_DIR (from pam_elogind); the default is root.
 */
#include <security/pam_appl.h>
#include <grp.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int conv(int n, const struct pam_message **msg,
		struct pam_response **resp, void *data)
{
	(void)n; (void)msg; (void)data;
	*resp = NULL;
	return PAM_SUCCESS;
}

static const char *const session_env[] = {
	"XDG_SESSION_TYPE=wayland",
	"XDG_SESSION_CLASS=user",
	"XDG_SESSION_DESKTOP=phosh",
	"XDG_SEAT=seat0",
	NULL,
};

int main(int argc, char **argv)
{
	struct pam_conv pc = { conv, NULL };
	const char *user = "root";
	struct passwd *pw;
	pam_handle_t *ph;
	char **env, vt[32];
	const char *active;
	FILE *f;
	pid_t pid;
	int i, st, r;

	if (argc > 2 && !strcmp(argv[1], "-u")) {
		user = argv[2];
		argv += 2;
		argc -= 2;
	}
	if (argc < 2) {
		fprintf(stderr, "usage: %s [-u user] command [args...]\n", argv[0]);
		return 2;
	}
	pw = getpwnam(user);
	if (!pw) {
		fprintf(stderr, "b8k-session: no user %s\n", user);
		return 1;
	}

	r = pam_start("b8k-session", user, &pc, &ph);
	if (r != PAM_SUCCESS) {
		fprintf(stderr, "pam_start: %d\n", r);
		return 1;
	}
	for (i = 0; session_env[i]; i++) {
		pam_putenv(ph, session_env[i]);
		putenv((char *)session_env[i]);
	}
	/* Bind the session to the active VT so logind treats it as foreground. */
	active = "tty1";
	f = fopen("/sys/class/tty/tty0/active", "r");
	if (f) {
		static char buf[16];

		if (fgets(buf, sizeof(buf), f)) {
			buf[strcspn(buf, "\n")] = 0;
			active = buf;
		}
		fclose(f);
	}
	snprintf(vt, sizeof(vt), "XDG_VTNR=%s", active + 3);
	pam_putenv(ph, vt);
	putenv(vt);
	pam_set_item(ph, PAM_TTY, active);

	r = pam_authenticate(ph, 0);
	if (r == PAM_SUCCESS)
		r = pam_acct_mgmt(ph, 0);
	if (r == PAM_SUCCESS)
		r = pam_open_session(ph, 0);
	if (r != PAM_SUCCESS) {
		fprintf(stderr, "pam: %s\n", pam_strerror(ph, r));
		pam_end(ph, r);
		return 1;
	}

	env = pam_getenvlist(ph);
	for (i = 0; env && env[i]; i++)
		putenv(env[i]);

	pid = fork();
	if (pid == 0) {
		setsid();
		if (pw->pw_uid != 0 &&
		    (initgroups(pw->pw_name, pw->pw_gid) ||
		     setgid(pw->pw_gid) || setuid(pw->pw_uid))) {
			perror("b8k-session: dropping privileges");
			_exit(126);
		}
		setenv("HOME", pw->pw_dir, 1);
		setenv("USER", pw->pw_name, 1);
		setenv("LOGNAME", pw->pw_name, 1);
		setenv("SHELL", pw->pw_shell, 1);
		if (chdir(pw->pw_dir))
			chdir("/");
		execvp(argv[1], argv + 1);
		perror(argv[1]);
		_exit(127);
	}
	signal(SIGINT, SIG_IGN);
	signal(SIGTERM, SIG_IGN);
	while (waitpid(pid, &st, 0) < 0)
		;
	pam_close_session(ph, 0);
	pam_end(ph, PAM_SUCCESS);
	return WIFEXITED(st) ? WEXITSTATUS(st) : 1;
}
