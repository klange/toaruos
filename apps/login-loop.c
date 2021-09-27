/**
 * @brief Repeatedly invoke `login` in a loop.
 *
 * This is more closely related to the 'getty' command in Linux
 * than our actual 'getty' command as it is also where we process
 * and display /etc/issue.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018-2021 K. Lange
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/utsname.h>
#include <sys/time.h>
#include <net/if.h>
#include <arpa/inet.h>

struct colorNames {
	const char * name;
	const char * output;
} ColorNames[] = {
	{"black", "\033[30m"},
	{"blue", "\033[34m"},
	{"bold", "\033[1m"},
	{"brown", "\033[33m"},
	{"cyan", "\033[36"},
	{"darkgray", "\033[90m"},
	{"gray", "\033[37m"},
	{"green", "\033[32m"},
	{"lightblue", "\033[94m"},
	{"lightcyan", "\033[96m"},
	{"lightgray", "\033[97m"},
	{"lightgreen", "\033[92m"},
	{"lightmagenta", "\033[95m"},
	{"lightred", "\033[91m"},
	{"magenta", "\033[35m"},
	{"red", "\033[31m"},
	{"reset", "\033[0m"},
	{"reverse", "\033[7m"},
	{"yellow", "\033[93m"},
	{NULL, NULL},
};

char * get_arg(FILE * f) {
	static char buf[32];
	int n = fgetc(f);
	if (n == '{') {
		int count = 0;
		do {
			int x = fgetc(f);
			if (x == '}') break;
			if (x < 0) break;
			buf[count++] = x;
			buf[count] = '\0';
		} while (count < 31);
		if (count) return buf;
	}
	return NULL;
}

char * get_ipv4_address(char * arg) {
	if (arg) {
		char if_path[1024];
		snprintf(if_path, 300, "/dev/net/%s", arg);
		int netdev = open(if_path, O_RDWR);
		if (netdev >= 0) {
			uint32_t ip_addr = 0;
			if (!ioctl(netdev, SIOCGIFADDR, &ip_addr)) {
				return inet_ntoa((struct in_addr){ntohl(ip_addr)});
			}
		}
	} else {
		/* Read /dev/net for interfaces */
		DIR * d = opendir("/dev/net");
		if (d) {
			struct dirent * ent;
			while ((ent = readdir(d))) {
				if (ent->d_name[0] == '.') continue;
				closedir(d);
				return get_ipv4_address(ent->d_name);
			}
			closedir(d);
		}
	}

	return "127.0.0.1";
}

void print_issue(void) {
	printf("\033[H\033[2J\n");

	FILE * f = fopen("/etc/issue","r");
	if (!f) return;

	/* Parse and display /etc/issue with support
	 * for some escape sequences that fill in
	 * dynamic information... */

	struct utsname u;
	uname(&u);

	struct tm * timeinfo;
	struct timeval now;
	gettimeofday(&now, NULL);
	timeinfo = localtime((time_t *)&now.tv_sec);

	static char buf[1024];

	while (!feof(f)) {
		int c = fgetc(f);

		if (c < 0) break; /* Probably EOF */

		if (c == '\\') {
			int next = fgetc(f);
			switch (next) {
				case '\n':
					/* A linefeed we should quietly skip. */
					continue;
				case '\\':
					/* A literal backslash */
					printf("\\");
					continue;

				/* Various things from uname */
				case 'n':
					printf("%s", u.nodename);
					continue;
				case 's':
					printf("%s", u.sysname);
					continue;
				case 'r':
					printf("%s", u.release);
					continue;
				case 'm':
					printf("%s", u.machine);
					continue;
				case 'v':
					printf("%s", u.version);
					continue;

				/* Various other useful things */
				case '4':
					printf("%s", get_ipv4_address(get_arg(f)));
					continue;
				case 'l':
					printf("%s", ttyname(STDIN_FILENO));
					continue;
				case 't':
					strftime(buf,1024,"%T %Z",timeinfo);
					printf("%s", buf);
					continue;
				case 'd':
					strftime(buf,1024,"%a %b %d %Y",timeinfo);
					printf("%s", buf);
					continue;

				/* Formatting stuff listed in Linux's getty(8) manpage */
				case 'e': {
					char * arg = get_arg(f);
					if (arg) {
						for (struct colorNames * cn = ColorNames; cn->name; cn++) {
							if (!strcmp(arg,cn->name)) {
								printf("%s", cn->output);
								break;
							}
						}
					} else {
						printf("\033");
					}
					continue;
				}
			}
		} else {
			printf("%c", c);
		}
	}


	fclose(f);
}

int main(int argc, char * argv[]) {
	while (1) {
		print_issue();
		pid_t f = fork();
		if (!f) {
			char * args[] = {
				"login",
				NULL
			};
			execvp(args[0], args);
		} else {
			int result, status;
			do {
				result = waitpid(f, &status, 0);
			} while (result < 0);
			if (WEXITSTATUS(status) == 2) break;
		}
	}

	return 1;
}
