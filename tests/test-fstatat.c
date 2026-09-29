#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <err.h>

int main(int argc, char * argv[]) {

	/* O_PATH test */
	int fd = openat(AT_FDCWD, "/etc/master.passwd", O_PATH); /* This should work */
	if (fd < 0) err(1, "openat");

	int fl = fcntl(fd, F_GETFL);
	if (fl < 0) err(1, "fcntl");
	fprintf(stderr, "GETFL=%#x\n", fl);

	struct stat st;
	int reta = fstat(fd, &st);
	if (reta < 0) err(1, "fstat");

	fprintf(stderr, "st.st_size = %lu\n", st.st_size);

	close(fd);

	int ret = fstatat(AT_FDCWD, "/usr/share/wallpaper.jpg", &st, AT_SYMLINK_NOFOLLOW);
	if (ret < 0) err(1, "fstatat");

	fprintf(stderr, "st.st_size = %lu\n", st.st_size); /* should be size of symlink */

	return 0;

}
