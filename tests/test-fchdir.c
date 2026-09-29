#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <err.h>

int main(int argc, char * argv[]) {
	int fd = open("/usr/share", O_DIRECTORY | O_PATH | O_CLOEXEC); /* should be good enough */
	if (fd < 0) err(1, "open");

	int res = fchdir(fd);
	if (res < 0) err(1, "fchdir");

	system("pwd");

	return 0;
}
