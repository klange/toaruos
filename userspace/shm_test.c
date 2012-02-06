/* vim:tabstop=4 shiftwidth=4 noexpandtab
 *
 * Hello World!
 */
#include <stdio.h>
#include <syscall.h>
#include <stdint.h>

DEFN_SYSCALL3(shm_negotiate, 35, char *, void *, uint32_t)

int main(int argc, char ** argv) {
	if (argc < 2) {
		fprintf(stderr, "%s: expected argument\n", argv[0]);
		return 1;
	}

	syscall_shm_negotiate(argv[1], NULL, 0x00);

	return 0;
}

