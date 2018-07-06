#include <stdio.h>
#include <syscall.h>
#include <errno.h>
#include <wait.h>
#include <string.h>

void set_console() {
	int _stdin  = syscall_open("/dev/null", 0, 0);
	int _stdout = syscall_open("/dev/ttyS0", 1, 0);
	int _stderr = syscall_open("/dev/ttyS0", 1, 0);

	if (_stdout < 0) {
		_stdout = syscall_open("/dev/null", 1, 0);
		_stderr = syscall_open("/dev/null", 1, 0);
	}

	(void)_stderr;
	(void)_stdin;
}

int start_options(char * args[]) {
	int pid = syscall_fork();
	if (!pid) {
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
		} while ((pid > 0) || (pid == -1 && errno == EINTR));
	}
	return 0;
}

int main(int argc, char * argv[]) {
	set_console();

	syscall_sethostname("base");

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
