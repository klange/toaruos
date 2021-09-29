#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

int main(int argc, char * argv[]) {
	int fd = open("/dev/dsp",O_WRONLY);
	if (fd < 0) {
		fprintf(stderr, "failed to open dsp\n");
		return 1;
	}
	return write(fd, &fd, -1);
}
