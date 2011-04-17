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
	char * bin = "/bin/echo";
	char * args = "herp";
	char * argv_[] = {
		bin,
		args,
		args,
		args,
		0
	};
	syscall_execve(bin, argv_, 0);
	return 0;
}
