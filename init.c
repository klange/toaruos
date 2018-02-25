#include <syscall.h>
#include <errno.h>
#include <_xlog.h>
#include <wait.h>

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
	_XLOG("Init starting...");
	set_console();

	char * _argv[] = {
		"/bin/compositor",
		NULL,
	};

	syscall_sethostname("base");
	return start_options(_argv);
}
