#include <syscall.h>

int main(int argc, char ** argv) {
	/* A Simple Shell */
	syscall_print("My PID is ");
	char x[] = {
		'0' + syscall_getpid(),
		0
	};
	syscall_print(x);
	syscall_print("\n");
	int i = syscall_getpid();
	for (int j = 0; j < 5; ++j) {
		syscall_fork();
		if (syscall_getpid() != i) {
			syscall_print("[Forked]\n");
			char * bin = "/bin/echo";
			char * args = "Executed echo.";
			char * argv_[] = {
				bin,
				args,
				0
			};
			syscall_execve(bin, argv_, 0);
		} else {
			syscall_print("(hello from parent)\n");
		}
	}
	return 0;
}
