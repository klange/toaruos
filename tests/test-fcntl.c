#include <stdio.h>
#include <fcntl.h>
#include <err.h>

int main(int argc, char * argv[]) {
	int fd = open("/tmp/append_file", O_CREAT | O_APPEND | O_CLOEXEC | O_WRONLY, 0660);

	if (fd < 0) err(1, "/tmp/append_file");

	fprintf(stderr, "GETFD flags = %#x (expect %#x)\n", fcntl(fd, F_GETFD), FD_CLOEXEC);
	fprintf(stderr, "GETFL flags = %#x (expect %#x)\n", fcntl(fd, F_GETFL), O_APPEND | O_WRONLY);

	return 0;
}
