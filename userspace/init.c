/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * init
 *
 * Provides the standard boot routines and
 * calls the user session (compositor / terminal)
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <syscall.h>

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
		setenv("HOST", buf, 1);
		fclose(_host_file);
	}
}

void start_terminal(char * arg) {
	int pid = fork();
	if (!pid) {
		char * tokens[] = {
			"/bin/terminal",
			"-F",
			arg,
			NULL
		};
		int i = execvp(tokens[0], tokens);
		exit(0);
	} else {
		syscall_wait(pid);
	}
}

void start_terminal_no_freetype(char * arg) {
	int pid = fork();
	if (!pid) {
		char * tokens[] = {
			"/bin/terminal",
			"-Fkb",
			arg,
			NULL
		};
		int i = execv(tokens[0], tokens);
		exit(0);
	} else {
		syscall_wait(pid);
	}
}

void start_vga_terminal(char * arg) {
	int pid = fork();
	if (!pid) {
		char * tokens[] = {
			"/bin/terminal",
			"-Vl",
			arg,
			NULL
		};
		int i = execvp(tokens[0], tokens);
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
		int i = execvp(tokens[0], tokens);
		exit(0);
	} else {
		syscall_wait(pid);
	}
}


int main(int argc, char * argv[]) {
	/* Hostname */
	set_hostname();
	if (argc > 1) {
		char * args = NULL;
		if (argc > 2) {
			args = argv[2];
		}
		if (!strcmp(argv[1],"--single")) {
			start_terminal(args);
			return 0;
		} else if (!strcmp(argv[1], "--vga")) {
			start_vga_terminal(args);
			return 0;
		} else if (!strcmp(argv[1], "--special")) {
			start_terminal_no_freetype(args);
			return 0;
		}
	}
	start_compositor();
}
