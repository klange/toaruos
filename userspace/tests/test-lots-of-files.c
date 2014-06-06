#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/wait.h>

#define CHUNK_SIZE 4096

void doit(int fd) {
	while (1) {
		char buf[CHUNK_SIZE];
		memset(buf, 0, CHUNK_SIZE);
		ssize_t r = read(fd, buf, CHUNK_SIZE);
		if (!r) return;
		write(STDOUT_FILENO, buf, r);
	}
}

int main(int argc, char * argv[]) {

	for (int i = 0; i < 500; ++i) {
		int fd = open("/proc/meminfo", O_RDONLY);
		doit(fd);
		close(fd);
	}

	return 0;
}
