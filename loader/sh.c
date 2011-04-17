#include <syscall.h>

int main(int argc, char ** argv) {
	/* A Simple Shell */
	int i = syscall_fork();
	if (i == 0) {
		syscall_print("Herpy derpy!\n");
	} else {
		syscall_print("Hello World!\n");
	}
	return 0;
}
