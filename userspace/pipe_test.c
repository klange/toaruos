/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * pipe test
 *
 * Makes a pipe. Pipes stuff to it. Yeah.
 */

#include <stdio.h>
#include <stdint.h>
#include <syscall.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

DECL_SYSCALL0(mkpipe);

int main(int argc, char ** argv) {
	int fd = syscall_mkpipe();
	printf("%d <- pipe\n", fd);
	int pid = getpid();
	uint32_t f = fork();
	if (getpid() != pid) {
		char buf[512];
		int r = read(fd, buf, 13);
		printf("[%d] %s\n", r, buf);
		return 0;
	} else {
		char * buf = "Hello world!";
		write(fd, buf, strlen(buf) + 1);
		return 0;
	}

	return 0;
}
