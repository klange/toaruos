/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2017 Kevin Lange
 *
 * kill
 *
 * Send a signal to another process
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

struct sig_def {
	int sig;
	const char * name;
};

struct sig_def signals[] = {
	{SIGHUP,"HUP"},
	{SIGINT,"INT"},
	{SIGQUIT,"QUIT"},
	{SIGILL,"ILL"},
	{SIGTRAP,"TRAP"},
	{SIGABRT,"ABRT"},
	{SIGEMT,"EMT"},
	{SIGFPE,"FPE"},
	{SIGKILL,"KILL"},
	{SIGBUS,"BUS"},
	{SIGSEGV,"SEGV"},
	{SIGSYS,"SYS"},
	{SIGPIPE,"PIPE"},
	{SIGALRM,"ALRM"},
	{SIGTERM,"TERM"},
	{SIGUSR1,"USR1"},
	{SIGUSR2,"USR2"},
	{SIGCHLD,"CHLD"},
	{SIGPWR,"PWR"},
	{SIGWINCH,"WINCH"},
	{SIGURG,"URG"},
	{SIGPOLL,"POLL"},
	{SIGSTOP,"STOP"},
	{SIGTSTP,"TSTP"},
	{SIGCONT,"CONT"},
	{SIGTTIN,"TTIN"},
	{SIGTTOUT,"TTOUT"},
	{SIGVTALRM,"VTALRM"},
	{SIGPROF,"PROF"},
	{SIGXCPU,"XCPU"},
	{SIGXFSZ,"XFSZ"},
	{SIGWAITING,"WAITING"},
	{SIGDIAF,"DIAF"},
	{SIGHATE,"HATE"},
	{SIGWINEVENT,"WINEVENT"},
	{SIGCAT,"CAT"},
	{0,NULL},
};

void usage(char * argv[]) {
	printf(
			"kill - send a signal to another process\n"
			"\n"
			"usage: %s [-\033[3mx\033[0m] \033[3mprocess\033[0m\n"
			"\n"
			" -h --help       \033[3mShow this help message.\033[0m\n"
			" -\033[3mx\033[0m              \033[3mSignal number to send\033[0m\n"
			"\n",
			argv[0]);
}

int main(int argc, char * argv[]) {
	int signum = SIGKILL;
	int pid = 0;

	if (argc < 2) {
		usage(argv);
		return 1;
	}

	if (argc > 2) {
		if (argv[1][0] == '-') {
			signum = -1;
			if (strlen(argv[1]+1) > 3 && !strncmp(argv[1]+1,"SIG",3)) {
				struct sig_def * s = signals;
				while (s->name) {
					if (!strcmp(argv[1]+4,s->name)) {
						signum = s->sig;
						break;
					}
					s++;
				}
			} else {
				if (argv[1][1] < '0' || argv[1][1] > '9') {
					struct sig_def * s = signals;
					while (s->name) {
						if (!strcmp(argv[1]+1,s->name)) {
							signum = s->sig;
							break;
						}
						s++;
					}
				} else {
					signum = atoi(argv[1]+1);
				}
			}
			if (signum == -1) {
				fprintf(stderr,"%s: %s: invalid signal specification\n",argv[0],argv[1]+1);
				return 1;
			}
		} else {
			usage(argv);
			return 1;
		}
		pid = atoi(argv[2]);
	} else if (argc == 2) {
		pid = atoi(argv[1]);
	}

	if (pid) {
		kill(pid, signum);
	}

	return 0;
}
