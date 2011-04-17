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
	syscall_fork();
	if (syscall_getpid() != i) {
		syscall_print("Herp!\n");
		char * bin = "/bin/echo";
		char * args = "von derpington";
		char * argv_[] = {
			bin,
			args,
			0
		};
		syscall_execve(bin, argv_, 0);
	} else {
		syscall_print("Awe shucks\n");
	}
	return 0;
}
