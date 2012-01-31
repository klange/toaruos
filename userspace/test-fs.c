/* vim:tabstop=4 shiftwidth=4 noexpandtab
 *
 * File System Test Suite
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>

int main(int argc, char ** argv) {
	printf("= Begin File System Testing =\n");

	int fd = creat("/test.log", S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

	printf("File descriptor generator: %d\n", fd);

	return 0;
}
