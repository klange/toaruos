#include <string.h>
#include <signal.h>
#include <stdio.h>

const char * const sys_siglist[] = {
	[SIGHUP]      = "Hangup",
	[SIGINT]      = "Interrupt",
	[SIGQUIT]     = "Quit",
	[SIGILL]      = "Illegal instruction",
	[SIGTRAP]     = "Trace/breakpoint trap",
	[SIGABRT]     = "Aborted",
	[SIGEMT]      = "Emulation trap",
	[SIGFPE]      = "Arithmetic exception",
	[SIGKILL]     = "Killed",
	[SIGBUS]      = "Bus error",
	[SIGSEGV]     = "Segmentation fault",
	[SIGSYS]      = "Bad system call",
	[SIGPIPE]     = "Broken pipe",
	[SIGALRM]     = "Alarm clock",
	[SIGTERM]     = "Terminated",
	[SIGUSR1]     = "User defined signal 1",
	[SIGUSR2]     = "User defined signal 2",
	[SIGCHLD]     = "Child process status",
	[SIGPWR]      = "Power failure",
	[SIGWINCH]    = "Window changed",
	[SIGURG]      = "Urgent I/O condition",
	[SIGPOLL]     = "Pollable event",
	[SIGSTOP]     = "Stopped",
	[SIGTSTP]     = "Stopped",
	[SIGCONT]     = "Continued",
	[SIGTTIN]     = "Stopped (tty input)",
	[SIGTTOU]     = "Stopped (tty output)",
	[SIGTTOUT]    = "Stopped (tty output)",
	[SIGVTALRM]   = "Virtual timer expired",
	[SIGPROF]     = "Profiling timer expired",
	[SIGXCPU]     = "CPU time limit exceeded",
	[SIGXFSZ]     = "File size limit exceeded",

	/* silly stuff */
	[SIGWAITING]  = "Waiting",
	[SIGDIAF]     = "Died in a fire",
	[SIGHATE]     = "Hated",
	[SIGWINEVENT] = "Window event",
	[SIGCAT]      = "Meow",
};

char * strsignal(int sig) {
	static char _signal_description[256];
	if (sig > 0 && sig < NUMSIGNALS) {
		snprintf(_signal_description, 256, "%s", sys_siglist[sig]);
	} else {
		snprintf(_signal_description, 256, "Killed by signal %d", sig);
	}
	return _signal_description;
}
