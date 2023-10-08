#include <errno.h>
#include <syscall.h>
#include <syscall_nums.h>
#include <sys/ioctl.h>
#include <unistd.h>

DEFN_SYSCALL3(ioctl, SYS_IOCTL, int, unsigned long, void *);

int ioctl(int fd, unsigned long request, void * argp) {
	__sets_errno(syscall_ioctl(fd, request, argp));
}

/* termios */
speed_t cfgetispeed(const struct termios * tio) {
	return 0;
}
speed_t cfgetospeed(const struct termios * tio) {
	return tio->c_cflag & CBAUD;
}

int cfsetispeed(struct termios * tio, speed_t speed) {
	/* hahahaha, yeah right */
	return 0;
}

int cfsetospeed(struct termios * tio, speed_t speed) {
	tio->c_cflag = (tio->c_cflag & ~CBAUD) | (speed & CBAUD);
	return 0;
}

int tcdrain(int i) {
	//DEBUG_STUB("tcdrain(%d)\n", i);
	return 0;
}

int tcflow(int fd, int arg) {
	return ioctl(fd, TCXONC, (void*)(uintptr_t)arg);
}

int tcflush(int fd, int arg) {
	return ioctl(fd, TCFLSH, (void*)(uintptr_t)arg);
}

pid_t tcgetsid(int fd) {
	//DEBUG_STUB("tcgetsid(%d)\n", fd);
	return getpid();
}

int tcsendbreak(int fd, int arg) {
	return ioctl(fd, TCSBRK, (void*)(uintptr_t)arg);
}

int tcgetattr(int fd, struct termios * tio) {
	return ioctl(fd, TCGETS, tio);
}

int tcsetattr(int fd, int actions, struct termios * tio) {
	switch (actions) {
		case TCSANOW:
			return ioctl(fd, TCSETS, tio);
		case TCSADRAIN:
			return ioctl(fd, TCSETSW, tio);
		case TCSAFLUSH:
			return ioctl(fd, TCSETSF, tio);
		default:
			return 0;
	}
}

int tcsetpgrp(int fd, pid_t pgrp) {
	return ioctl(fd, TIOCSPGRP, &pgrp);
}

pid_t tcgetpgrp(int fd) {
	pid_t pgrp;
	ioctl(fd, TIOCGPGRP, &pgrp);
	return pgrp;
}

