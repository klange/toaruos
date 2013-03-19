#include <stdio.h>
#include <sys/ioctl.h>

int main(int argc, char * argv) {
	struct winsize w;
	ioctl(0, TIOCGWINSZ, &w);

	printf("This terminal has %d rows and %d columns.\n", w.ws_col, w.ws_row);
	return 0;
}
