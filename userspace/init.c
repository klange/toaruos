/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Initial Startup
 */

#include <stdio.h>
#include <syscall.h>

DEFN_SYSCALL1(sethostname, 31, char *);

#define DEFAULT_HOSTNAME "toaru-test"
#define FORK_FOR_TERMINAL 0
#define FREETYPE 1

/* Set the hostname to whatever is in /etc/hostname */
void set_hostname() {
	FILE * _host_file = fopen("/etc/hostname", "r");
	if (!_host_file) {
		/* No /etc/hostname, use the default */
		syscall_sethostname(DEFAULT_HOSTNAME);
	} else {
		char buf[256];
		fgets(buf, 255, _host_file);
		syscall_sethostname(buf);
		fclose(_host_file);
	}
}

void start_terminal() {
#if FORK_FOR_TERMINAL
	int pid = fork();
	if (!pid) {
#endif
	char * tokens[] = {
		"/bin/terminal",
#if FREETYPE
		"-f",
#endif
		NULL
	};
	int i = execve(tokens[0], tokens, NULL);
	exit(0);
#if FORK_FOR_TEMRINAL
	}
#endif
}

void main(int argc, char * argv[]) {
	/* Hostname */
	set_hostname();
	/* Terminal */
	start_terminal();
}
