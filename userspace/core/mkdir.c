/* vim:tabstop=4 shiftwidth=4 noexpandtab
 *
 * mkdir
 *
 * Create a directory.
 */
#include <stdio.h>
#include <syscall.h>
#include <stdint.h>

int main(int argc, char ** argv) {
	if (argc < 2) {
		fprintf(stderr, "%s: expected argument\n", argv[0]);
		return 1;
	}

	syscall_mkdir(argv[1], 0x00);

	return 0;
}
