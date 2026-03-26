#include <signal.h>
#include <string.h>
#include <stdlib.h>

const char * signal_names[] = {
	[0] = "EXIT",
	[SIGHUP] = "HUP",
	[SIGINT] = "INT",
	[SIGQUIT] = "QUIT",
	[SIGILL] = "ILL",
	[SIGTRAP] = "TRAP",
	[SIGABRT] = "ABRT",
	[SIGEMT] = "EMT",
	[SIGFPE] = "FPE",
	[SIGKILL] = "KILL",
	[SIGBUS] = "BUS",
	[SIGSEGV] = "SEGV",
	[SIGSYS] = "SYS",
	[SIGPIPE] = "PIPE",
	[SIGALRM] = "ALRM",
	[SIGTERM] = "TERM",
	[SIGUSR1] = "USR1",
	[SIGUSR2] = "USR2",
	[SIGCHLD] = "CHLD",
	[SIGPWR] = "PWR",
	[SIGWINCH] = "WINCH",
	[SIGURG] = "URG",
	[SIGPOLL] = "POLL",
	[SIGSTOP] = "STOP",
	[SIGTSTP] = "TSTP",
	[SIGCONT] = "CONT",
	[SIGTTIN] = "TTIN",
	[SIGTTOUT] = "TTOUT",
	[SIGVTALRM] = "VTALRM",
	[SIGPROF] = "PROF",
	[SIGXCPU] = "XCPU",
	[SIGXFSZ] = "XFSZ",
	[SIGWAITING] = "WAITING",
	[SIGDIAF] = "DIAF",
	[SIGHATE] = "HATE",
	[SIGWINEVENT] = "WINEVENT",
	[SIGCAT] = "CAT",
	[SIGTTOU] = "TTOU",
};

int sig2str(int signum, char * str) {
	if (signum < 0) return -1;
	if (signum >= NUMSIGNALS) return -1;

	strcpy(str, signal_names[signum]);
	return 0;
}

int str2sig(const char *restrict str, int *restrict pnum) {

	/* Just get this over with first. */
	for (int i = 0; i < NUMSIGNALS; ++i) {
		if (!strcmp(str, signal_names[i])) {
			*pnum = i;
			return 0;
		}
	}

	/* We don't support RTMIN/RTMAX, so we'll skip those, so next is decimal forms. */
	int is_dec = 1;
	for (const char *s = str; *s; s++) {
		if (*s < '0' || *s > '9') {
			is_dec = 0;
			break;
		}
	}

	if (is_dec) {
		int signum = atoi(str);
		if (signum < 1 || signum >= NUMSIGNALS) return -1;
		*pnum = signum;
		return 0;
	}

	/* If we "recognize[...] the string as an additional implementation-dependent form of signal name",
	 * we are allowed to handle it here. I think it would be useful if this accepted SIG* forms of the
	 * names as well, so let's do that. */
	if (strstr(str,"SIG") == str) {
		for (int i = 0; i < NUMSIGNALS; ++i) {
			if (!strcmp(str+3, signal_names[i])) {
				*pnum = i;
				return 0;
			}
		}
	}

	return -1;
}
