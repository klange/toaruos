#include <syscall.h>

int main(int argc, char ** argv) {
	char * str = "Hello world!\n";
	syscall_write(1 /* stdout */, str, 13);
	return 0;
}

/*
 * vim:tabstop=4
 * vim:noexpandtab
 * vim:shiftwidth=4
 */
