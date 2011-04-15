#include <syscall.h>

int main(int argc, char ** argv) {
	char * yes = "y";
	if (argc > 1) {
		yes = argv[1];
	}
	while (1) {
		syscall_print(yes);
		syscall_print("\n");
	}
	return 0;
}

/*
 * vim:tabstop=4
 * vim:noexpandtab
 * vim:shiftwidth=4
 */
