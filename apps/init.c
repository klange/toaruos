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
}

int start_options(char * args[]) {
	int pid = syscall_fork();
	if (!pid) {
		char * _envp[] = {
			"LD_LIBRARY_PATH=/lib",
			"HOME=/",
			"PATH=/bin",
			"USER=root",
			"PRETEND_STDOUT_IS_TTY=1",
			"WM_THEME=fancy",
			NULL,
		};
		int i = syscall_execve(args[0], args, _envp);
		syscall_exit(0);
	} else {
		int pid = 0;
		do {
			pid = wait(NULL);
		} while ((pid > 0) || (pid == -1 && errno == EINTR));
	}
}

int main(int argc, char * argv[]) {
	set_console();

	char * _argv[] = {
		"/bin/compositor",
		NULL,
	};

	syscall_sethostname("base");

	if (argc > 1) {
		char * args = NULL;
		if (argc > 2) {
			args = argv[2];
		}
		if (!strcmp(argv[1], "--vga")) {
			return start_options((char *[]){"/bin/terminal-vga","-l",NULL});
#if 0
		} else if (!strcmp(argv[1], "--migrate")) {
			return start_options((char *[]){"/usr/bin/python","/bin/migrate.py",NULL});
#endif
		} else {
			/* Pass it to the compositor... */
			return start_options((char *[]){"/bin/compositor","--",argv[1],NULL});
		}
	}
	return start_options((char *[]){"/bin/compositor",NULL});
}
