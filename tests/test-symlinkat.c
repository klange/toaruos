#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <err.h>

int main(int argc, char * argv[]) {
	int fd = open("/tmp", O_DIRECTORY | O_RDONLY);
	if (fd < 0) err(1, "open");

	int ret = symlinkat("/usr/share", fd, "sym");
	if (ret < 0) err(1, "symlinkat");

	int nfd = open("/tmp/sym", O_DIRECTORY | O_RDONLY);
	if (nfd < 0) err(1, "open 2");

	return 0;
}
