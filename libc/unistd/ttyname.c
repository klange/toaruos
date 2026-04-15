#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>


int ttyname_r(int fd, char * buf, size_t buflen) {
	struct __tty_name data;
	data.len = buflen;
	data.buf = buf;
	return ioctl(fd, IOCTLTTYNAME, &data);
}

char * ttyname(int fd) {
	static char __tty_name[30]; /* only needs to hold /dev/pty/ttyXXXXXXX */
	if (ttyname_r(fd, __tty_name, 30) == -1) return NULL;
	return __tty_name;
}
