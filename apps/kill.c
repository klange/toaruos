/**
 * @brief kill - Send a signal to a process
 *
 * Supports signal names like any mature `kill` should.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2018 K. Lange
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <ctype.h>
#include <errno.h>
#include <sys/wait.h>

int usage(char * argv[]) {
#define X_S "\033[3m"
#define X_E "\033[0m"
	fprintf(stderr,
			"%s - send a signal to another process\n"
			"\n"
			"usage: %s [-" X_S "name" X_E "] " X_S "pid..." X_E "\n"
			"       %s -l [" X_S "exit_status" X_E "]\n"
			"\n"
			" --help          " X_S "Show this help message." X_E "\n"
			" -s " X_S "name         Specify the signal to send." X_E "\n"
			" -l              " X_S "List available signals." X_E "\n"
			" -l " X_S "exit_status  Interpret an exit status or signal number and print its name." X_E "\n"
			" -" X_S "name           Same as -s name." X_E "\n"
			"\n"
			"Signals may be named with or without the SIG prefix, or numerically.\n",
			argv[0], argv[0], argv[0]);
	return 1;
}

int main(int argc, char * argv[]) {
	/* Special getopt-alike argument parsing */

	int optind = 1;
	char * signal_name = NULL;

	while (optind < argc) {
		if (*argv[optind] == '-') {
			switch (argv[optind][1]) {
				case 'l':
					if (signal_name) return usage(argv);
					if (argv[optind][2] != '\0') {
						signal_name = &argv[optind][1];
						goto _next_opt;
					}
					/* Assume -l flag */
					char signame[SIG2STR_MAX] = {0};
					if (optind + 1 == argc) {
						/* Print everything */
						for (int i = 1; i < NSIG; ++i) {
							if (!sig2str(i, signame)) {
								fprintf(stdout, "%s%c", signame, i + 1 == NSIG ? '\n' : ' ');
							}
						}
						return 0;
					} else {
						if (optind + 2 != argc) return usage(argv);
						int status = atoi(argv[optind+1]);

						if (status < NSIG) {
							if (!sig2str(status, signame)) {
								fprintf(stdout, "%s\n", signame);
								return 0;
							}
						}

						if (status > 128 && status - 128 < NSIG) {
							if (!sig2str(status-128,signame)) {
								fprintf(stdout, "%s\n", signame);
								return 0;
							}
						}

						return 1;
					}
					break;
				case 's':
					if (signal_name) return usage(argv);
					if (argv[optind][2] != '\0') {
						/* Assume signal name */
						signal_name = &argv[optind][1];
						goto _next_opt;
					} else {
						/* Signal must be next argument */
						if (optind + 1 == argc) return usage(argv);
						signal_name = argv[optind+1];
						optind++; /* Argument */
						goto _next_opt;
					}
					break;
				case '0' ... '9':
				case 'a' ... 'k':
				case 'm' ... 'r':
				case 't' ... 'z':
				case 'A' ... 'Z':
					if (signal_name) return usage(argv);
					/* Signal name */
					signal_name = &argv[optind][1];
					goto _next_opt;
				case '-':
					/* End of arguments or long arguments */
					if (!argv[optind][2]) {
						optind++;
						goto _end_opt;
					} else if (!strcmp(&argv[optind][2],"help")) {
						usage(argv);
						return 0;
					} else {
						fprintf(stderr, "%s: '%s' is not a recognized long option.\n", argv[0], argv[optind]);
						return usage(argv);
					}
				default:
					return usage(argv);
			}
		} else {
			break;
		}

_next_opt:
		optind++;
		continue;
_end_opt:
		break;
	}

	if (optind == argc) {
		return usage(argv);
	}

	int signum = SIGKILL;
	if (signal_name) {
		/* Convert name to upper case */
		for (char * s = signal_name; *s; ++s) {
			*s = toupper(*s);
		}

		/* Get signal from str2sig */
		if (str2sig(signal_name, &signum)) {
			fprintf(stderr, "%s: '%s' is not an understood signal.\n", argv[0], signal_name);
			return 1;
		}
	}

	int retval = 0;

	for (int i = optind; i < argc; ++i) {
		int pid = atoi(argv[i]);
		if (pid) {
			if (kill(pid, signum) < 0) {
				fprintf(stderr, "%s: (%d) %s\n", argv[0], pid, strerror(errno));
				retval = 1;
			}
		} else {
			fprintf(stderr, "%s: invalid pid (%s)\n", argv[0], argv[i]);
			retval = 1;
		}
	}

	return retval;
}
