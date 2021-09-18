#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>

static char _tty_name[30]; /* only needs to hold /dev/pty/ttyXXXXXXX */

char * ttyname(int fd) {

	if (!isatty(fd)) {
		errno = ENOTTY;
		return NULL;
	}

	ioctl(fd, IOCTLTTYNAME, _tty_name);

	return _tty_name;
}

int ttyname_r(int fd, char * buf, size_t buflen) {
	if (!isatty(fd)) return ENOTTY;
	if (buflen < 30) return ERANGE;
	ioctl(fd, IOCTLTTYNAME, buf);
	return 0;
}
