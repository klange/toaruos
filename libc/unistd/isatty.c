#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/termios.h>

int isatty(int fd) {
	struct winsize ws;
	return ioctl(fd, TIOCGWINSZ, &ws) + 1;
}
