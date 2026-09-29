#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <err.h>
#include <sys/stat.h>

int main(int argc, char * argv[]) {
	int dirfd = openat(AT_FDCWD, "/usr/share", O_DIRECTORY | O_RDONLY | O_CLOEXEC);
	if (dirfd < 0) err(1, "openat, directory");

	fprintf(stderr, "dirfd = %d\n", dirfd);

	int ffd = openat(dirfd, "logo_login.png", O_RDONLY);
	if (ffd < 0) err(1, "openat, file");

	int bad = openat(ffd, "anything", O_RDONLY);
	if (bad >= 0) errx(2, "unexpected success using file as dirfd");
	if (errno != ENOTDIR) err(2, "unexpected error type");

	int cfd = openat(dirfd, "../../tmp/farts", O_CREAT | O_WRONLY | O_EXCL, 0666);
	if (cfd < 0) err(1, "openat, creat");

	write(cfd, "hello\n", 6);
	close(cfd);
	close(ffd);

	return 0;
}
