#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <err.h>

int main(int argc, char * argv[]) {
	int fd = open("/tmp", O_DIRECTORY | O_PATH);
	if (fd < 0) err(1, "open");

	int res = mkdirat(fd, "test", 0766);
	if (res < 0) err(1, "mkdirat, 1");

	/* We can also do this */
	int dirfd = openat(fd, "farts", O_DIRECTORY | O_CREAT, 0766);
	if (dirfd < 0) err(1, "openat");

	res = mkdirat(dirfd, "thing", 0766);
	if (res < 0) err(1, "mkdirat, 2");

	return 0;
}
