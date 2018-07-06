#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>

int main(int argc, char * argv[]) {
	if (argc < 3) {
		fprintf(stderr, "Usage: ttysize COLS ROWS\n");
		return 1;
	}

	struct winsize w;
	w.ws_col = atoi(argv[1]);
	w.ws_row = atoi(argv[2]);
	w.ws_xpixel = 0;
	w.ws_ypixel = 0;
	ioctl(0, TIOCSWINSZ, &w);
	return 0;
}
