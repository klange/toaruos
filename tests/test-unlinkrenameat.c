#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <err.h>

int main(int argc, char * argv[]) {
	int fd = open("/tmp", O_DIRECTORY | O_PATH);
	if (fd < 0) err(1, "open");

	int a = openat(fd, "test_a", O_CREAT | O_WRONLY, 0777);
	if (a < 0) err(1, "openat");
	dprintf(a, "Hello, world.\n");
	close(a);

	int ret = renameat(fd, "test_a", fd, "test_b");
	if (ret < 0) err(1, "renameat");

	int home = open("/home/local", O_DIRECTORY | O_PATH);
	if (home < 0) err(1, "open home");

	int bad = renameat(fd, "test_b", home, "oops");
	if (bad >= 0) err(1, "unexpected success");
	warn("should be 'Invalid cross-device link'");

	ret = unlinkat(fd, "test_b", 0);
	if (ret < 0) err(1, "unlinkat");

	fprintf(stderr, "done\n");
	return 0;
}
