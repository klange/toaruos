#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <kernel/video.h>

int main(int argc, char * argv[]) {
	if (argc < 3) {
		fprintf(stderr, "Usage: %s width height\n", argv[0]);
	}

	/* This should be something we do through yutani */
	int fd = open("/dev/fb0", O_RDONLY);

	if (fd < 0) {
		perror("open");
		return 1;
	}

	struct vid_size s;
	s.width = atoi(argv[1]);
	s.height = atoi(argv[2]);

	if (ioctl(fd, IO_VID_SET, &s) < 0) {
		perror("ioctl");
		return 1;
	}

	return 0;
}
