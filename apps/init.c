#include <stdio.h>
#include <syscall.h>
#include <errno.h>
#include <wait.h>
#include <string.h>
#include <unistd.h>

void set_console(void) {
	int _stdin  = syscall_open("/dev/ttyS0", 0, 0);
	int _stdout = syscall_open("/dev/ttyS0", 1, 0);
	int _stderr = syscall_open("/dev/ttyS0", 1, 0);

	if (_stdout < 0) {
		_stdout = syscall_open("/dev/null", 1, 0);
		_stderr = syscall_open("/dev/null", 1, 0);
	}

	(void)_stderr;
	(void)_stdin;
}

void set_hostname(void) {
	FILE * f = fopen("/etc/hostname", "r");

	if (!f) {
		/* set fallback hostname */
		sethostname("localhost", 4);
	} else {
		char tmp[128];
		fgets(tmp, 128, f);
		char * nl = strchr(tmp, '\n');
		if (nl) *nl = '\0';
		sethostname(tmp, strlen(tmp));
	}

}

int start_options(char * args[]) {
	int cpid = syscall_fork();
	if (!cpid) {
		char * _envp[] = {
			"LD_LIBRARY_PATH=/lib:/usr/lib",
			"PATH=/bin:/usr/bin",
			"USER=root",
			"HOME=/home/root",
			NULL,
		};
		syscall_execve(args[0], args, _envp);
		syscall_exit(0);
	} else {
		int pid = 0;
		do {
			pid = wait(NULL);
			if (pid == cpid) {
				break;
			}
		} while ((pid > 0) || (pid == -1 && errno == EINTR));
	}

	syscall_reboot();

	return 0;
}

int main(int argc, char * argv[]) {
	set_console();
	set_hostname();

	if (argc > 1) {
		if (!strcmp(argv[1], "--vga")) {
			return start_options((char *[]){"/bin/terminal-vga","-l",NULL});
		} else if (!strcmp(argv[1], "--migrate")) {
			return start_options((char *[]){"/bin/migrate",NULL});
		} else if (!strcmp(argv[1], "--headless")) {
			return start_options((char *[]){"/bin/getty",NULL});
		} else {
			/* Pass it to the compositor... */
			return start_options((char *[]){"/bin/compositor","--",argv[1],NULL});
		}
	}
	return start_options((char *[]){"/bin/compositor",NULL});
}
