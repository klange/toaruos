/* vim: ts=4 sw=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 *
 * killall
 *
 * Find processes by name and send them signals.
 */


#include <sys/stat.h>
#include <sys/signal.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <signal.h>
#include <getopt.h>

typedef struct process {
	int pid;
	int ppid;
	int tgid;
	char name[100];
	char path[200];
} p_t;

#define LINE_LEN 4096

p_t * build_entry(struct dirent * dent) {
	char tmp[256];
	FILE * f;
	char line[LINE_LEN];

	sprintf(tmp, "/proc/%s/status", dent->d_name);
	f = fopen(tmp, "r");

	p_t * proc = malloc(sizeof(p_t));

	while (fgets(line, LINE_LEN, f) != NULL) {
		char * n = strstr(line,"\n");
		if (n) { *n = '\0'; }
		char * tab = strstr(line,"\t");
		if (tab) {
			*tab = '\0';
			tab++;
		}
		if (strstr(line, "Pid:") == line) {
			proc->pid = atoi(tab);
		} else if (strstr(line, "PPid:") == line) {
			proc->ppid = atoi(tab);
		} else if (strstr(line, "Tgid:") == line) {
			proc->tgid = atoi(tab);
		} else if (strstr(line, "Name:") == line) {
			strcpy(proc->name, tab);
		} else if (strstr(line, "Path:") == line) {
			strcpy(proc->path, tab);
		}
	}

	if (strstr(proc->name,"python") == proc->name) {
		char * name = proc->path + strlen(proc->path) - 1;

		while (1) {
			if (*name == '/') {
				name++;
				break;
			}
			if (name == proc->name) break;
			name--;
		}

		memcpy(proc->name, name, strlen(name)+1);
	}

	if (proc->tgid != proc->pid) {
		char tmp[100] = {0};
		sprintf(tmp, "{%s}", proc->name);
		memcpy(proc->name, tmp, strlen(tmp)+1);
	}

	fclose(f);

	return proc;
}

void show_usage(int argc, char * argv[]) {
	printf(
			"killall - send signal to processes with given name\n"
			"\n"
			"usage: %s [-s SIG] name\n"
			"\n"
			" -s     \033[3msignal to send\033[0m\n"
			" -?     \033[3mshow this help text\033[0m\n"
			"\n", argv[0]);
}

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

int main (int argc, char * argv[]) {

	int signum = SIGTERM;

	/* Open the directory */
	DIR * dirp = opendir("/proc");

	char c;
	while ((c = getopt(argc, argv, "s:?")) != -1) {
		switch (c) {
			case 's':
				{
					signum = -1;
					if (strlen(optarg) > 3 && strstr(optarg,"SIG") == (optarg)) {
						struct sig_def * s = signals;
						while (s->name) {
							if (!strcmp(optarg+3,s->name)) {
								signum = s->sig;
								break;
							}
							s++;
						}
					} else {
						if (optarg[0] < '0' || optarg[0] > '9') {
							struct sig_def * s = signals;
							while (s->name) {
								if (!strcmp(optarg,s->name)) {
									signum = s->sig;
									break;
								}
								s++;
							}
						} else {
							signum = atoi(optarg);
						}
					}
					if (signum == -1) {
						fprintf(stderr,"%s: %s: invalid signal specification\n",argv[0],optarg);
						return 1;
					}
				}
				break;
			case '?':
				show_usage(argc, argv);
				return 0;
		}
	}

	if (optind >= argc) {
		show_usage(argc, argv);
		return 1;
	}

	int killed_something = 0;

	struct dirent * ent = readdir(dirp);
	while (ent != NULL) {
		if (ent->d_name[0] >= '0' && ent->d_name[0] <= '9') {
			p_t * proc = build_entry(ent);

			if (!strcmp(proc->name, argv[optind])) {
				kill(proc->pid, signum);
				killed_something = 1;
			}
		}
		ent = readdir(dirp);
	}
	closedir(dirp);

	if (!killed_something) {
		fprintf(stderr, "%s: no process found\n", argv[optind]);
		return 1;
	}
	return 0;
}

