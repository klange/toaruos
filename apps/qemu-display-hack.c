/*
 * Daemon to communicate resolution changes with QEMU over serial.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <kernel/video.h>

int main(int argc, char * argv[]) {

	if (system("qemu-fwcfg -q opt/org.toaruos.displayharness") != 0) {
		fprintf(stderr, "%s: display harness not enabled\n", argv[0]);
		return 1;
	}

	int fd = open("/dev/fb0", O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "%s: failed to open framebuffer: %s\n", argv[0], strerror(errno));
		return 1;
	}

	struct vid_size s;

	FILE * f = fopen("/dev/ttyS1","r+");
	if (!f) {
		fprintf(stderr, "%s: failed to open serial: %s\n", argv[0], strerror(errno));
		return 1;
	}

	if (!fork()) {

		while (!feof(f)) {
			char data[128];
			fgets(data, 128, f);

			char * linefeed = strstr(data,"\n");
			if (linefeed) { *linefeed = '\0'; }

			char * width;
			char * height;

			width = strstr(data, " ");
			if (width) {
				*width = '\0';
				width++;
			} else {
				continue; /* bad line */
			}

			height = strstr(width, " ");
			if (height) {
				*height = '\0';
				height++;
			} else {
				continue; /* bad line */
			}

			s.width = atoi(width);
			s.height = atoi(height);

			ioctl(fd, IO_VID_SET, &s);
			fprintf(f, "X");
			fflush(f);
		}

		return 0;
	}

	return 0;
}
