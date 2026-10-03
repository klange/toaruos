#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <err.h>

int main(int argc, char * argv[]) {
	int fd = open("/proc/self/comm", O_WRONLY);
	if (fd < 0) err(1, "open");

	if (write(fd, "derp", 4) < 0) err(1, "write");

	close(fd);

	char *tmp;
	asprintf(&tmp, "cat /proc/%d/comm -", getpid());

	return system(tmp);
}

