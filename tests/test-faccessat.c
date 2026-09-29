#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <err.h>

int main(int argc, char * argv[]) {
	int fd = open("/etc", O_DIRECTORY | O_RDONLY);
	if (fd < 0) err(1, "open");

	int ret = faccessat(fd, "master.passwd", F_OK, 0);
	if (ret < 0) err(1, "faccessat F_OK");

	ret = faccessat(fd, "master.passwd", R_OK, 0);
	if (ret < 0) err(1, "faccessat R_OK");

	return 0;
}
