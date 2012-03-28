/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Initial Startup
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <syscall.h>

DEFN_SYSCALL1(sethostname, 31, char *);

#define DEFAULT_HOSTNAME "toaru-test"

/* Set the hostname to whatever is in /etc/hostname */
void set_hostname() {
	FILE * _host_file = fopen("/etc/hostname", "r");
	if (!_host_file) {
		/* No /etc/hostname, use the default */
		syscall_sethostname(DEFAULT_HOSTNAME);
	} else {
		char buf[256];
		fgets(buf, 255, _host_file);
		if (buf[strlen(buf)-1] == '\n') {
			buf[strlen(buf)-1] = '\0';
		}
		syscall_sethostname(buf);
		fclose(_host_file);
	}
}

void start_terminal() {
	int pid = fork();
	if (!pid) {
		char * tokens[] = {
			"/bin/terminal",
			"-F",
			NULL
		};
		int i = execve(tokens[0], tokens, NULL);
		exit(0);
	} else {
		syscall_wait(pid);
	}
}

void start_compositor() {
	int pid = fork();
	if (!pid) {
		char * tokens[] = {
			"/bin/compositor",
			NULL
		};
		int i = execve(tokens[0], tokens, NULL);
		exit(0);
	} else {
		syscall_wait(pid);
	}
}


void main(int argc, char * argv[]) {
	fprintf(stderr, "[init] Hello world.\n");
	/* Hostname */
	set_hostname();
	if (argc > 1 && !strcmp(argv[1],"--single")) {
		/* Terminal */
		start_terminal();
	} else {
		/* Compositor */
		start_compositor();
	}
}
