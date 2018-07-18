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

	fprintf(stderr, "Display harness is running.\n");

	int fd = open("/dev/fb0", O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "%s: failed to open framebuffer: %s\n", argv[0], strerror(errno));
		return 1;
	}

	fprintf(stderr, "Framebuffer opened.\n");

	struct vid_size s;

	FILE * f = fopen("/dev/ttyS0","r+");
	if (!f) {
		fprintf(stderr, "%s: failed to open serial: %s\n", argv[0], strerror(errno));
	}

	fprintf(stderr, "Serial opened.\n");

	if (!fork()) {

		while (!feof(f)) {
			char data[128];
			fgets(data, 128, f);

			char * linefeed = strstr(data,"\n");
			if (linefeed) { *linefeed = '\0'; }

			fprintf(stderr, "Received a line from serial: [%s]\n", data);

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

			fprintf(stderr, "Setting resolution to %d x %d\n", (int)s.width, (int)s.height);

			ioctl(fd, IO_VID_SET, &s);
			fprintf(f, "X");
			fflush(f);
		}

		return 0;
	}

	fprintf(stderr, "Disconnecting daemon.\n");

	return 0;
}
