#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <syscall.h>

static struct termios old;

static void set_unbuffered() {
	tcgetattr(fileno(stdin), &old);
	struct termios new = old;
	new.c_lflag &= (~ICANON & ~ECHO);
	tcsetattr(fileno(stdin), TCSAFLUSH, &new);
}

static void set_buffered() {
	tcsetattr(fileno(stdin), TCSAFLUSH, &old);
}

static int getc_timeout(FILE * f, int timeout) {
	int fds[1] = {fileno(f)};
	int index = syscall_fswait2(1,fds,timeout);
	if (index == 0) {
		return fgetc(f);
	} else {
		return -1;
	}
}

static void divine_size(int * width, int * height) {
	set_unbuffered();

	*width = 80;
	*height = 24;

	fprintf(stderr, "\033[s\033[1000;1000H\033[6n\033[H");
	fflush(stderr);

	char buf[1024] = {0};
	size_t i = 0;
	while (1) {
		char c = getc_timeout(stdin, 200);
		if (c == 'R') break;
		if (c == -1) goto _done;
		if (c == '\033') continue;
		if (c == '[') continue;
		buf[i++] = c;
	}

	char * s = strstr(buf, ";");
	if (s) {
		*(s++) = '\0';

		*height = atoi(buf);
		*width = atoi(s);
	}

_done:
	fprintf(stderr,"\033[u");
	fflush(stderr);
	set_buffered();
}

int main(int argc, char * argv[]) {
	int width, height;

	if (argc < 3) {
		divine_size(&width, &height);
	} else {
		width = atoi(argv[1]);
		height = atoi(argv[2]);
	}

	struct winsize w;
	w.ws_col = width;
	w.ws_row = height;
	w.ws_xpixel = 0;
	w.ws_ypixel = 0;
	ioctl(0, TIOCSWINSZ, &w);

	fprintf(stderr, "%dx%d\n", width, height);

	return 0;
}
