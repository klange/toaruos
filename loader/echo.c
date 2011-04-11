#include <syscall.h>

int main(int argc, char ** argv) {
	for (int i = 1; i < argc; ++i) {
		syscall_print(argv[i]);
		if (i != argc - 1) {
			syscall_print(" ");
		}
	}
	syscall_print("\n");
	return 0;
}

/*
 * vim:tabstop=4
 * vim:noexpandtab
 * vim:shiftwidth=4
 */
