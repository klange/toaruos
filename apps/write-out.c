#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

int main(int argc, char * argv[]) {
	int fd = open(argv[1], O_WRONLY | O_CREAT, 0666);

	while (1) {
		char buf[1024];
		size_t r = read(0, buf, 1024);
		if (r == 0) break;
		write(fd, buf, r);
	}

}
