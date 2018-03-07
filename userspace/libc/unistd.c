#include <unistd.h>
#include <syscall.h>
#include <va_list.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <signal.h>
#include <string.h>

#ifndef syscall_pipe
DECL_SYSCALL1(pipe,  int *);
#endif

#ifndef syscall_readline
DECL_SYSCALL3(readlink, char *, char *, int);
#endif

int open(const char *name, int flags, ...) {
	va_list argp;
	int mode;
	int result;
	va_start(argp, flags);
	if (flags & O_CREAT) mode = va_arg(argp, int);
	va_end(argp);

	result = syscall_open(name, flags, mode);
	if (result == -1) {
		if (flags & O_CREAT) {
			errno = EACCES;
		} else {
			errno = ENOENT;
		}
	} else if (result < 0) {
		errno = -result;
	}
	return result;
}

int read(int file, void *ptr, size_t len) {
	return syscall_read(file,ptr,len);
}

int creat(const char *path, mode_t mode) {
	return open(path, O_WRONLY|O_CREAT|O_TRUNC, mode);
}

int close(int file) {
	return syscall_close(file);
}

int link(const char *old, const char *new) {
	errno = EMLINK;
	return -1;
}

off_t lseek(int file, off_t ptr, int dir) {
	return syscall_lseek(file,ptr,dir);
}

int isatty(int fd) {
	int dtype = ioctl(fd, IOCTLDTYPE, NULL);
	if (dtype == IOCTL_DTYPE_TTY) return 1;
	errno = EINVAL;
	return 0;
}

ssize_t write(int file, const void *ptr, size_t len) {
	return syscall_write(file,(char *)ptr,len);
}

pid_t getpid(void) {
	return syscall_getpid();
}

int dup2(int oldfd, int newfd) {
	return syscall_dup2(oldfd, newfd);
}

pid_t fork(void) {
	return syscall_fork();
}

void exit(int val) {
	_exit(val);
}

int kill(int pid, int sig) {
	return syscall_send_signal(pid, sig);
}

sighandler_t signal(int signum, sighandler_t handler) {
	return (sighandler_t)syscall_signal(signum, (void *)handler);
}

int uname(struct utsname *__name) {
	return syscall_uname((void *)__name);
}

int chdir(const char *path) {
	return syscall_chdir((char*)path);
}

char *getcwd(char *buf, size_t size) {
	if (!buf) buf = malloc(size);
	return (char *)syscall_getcwd(buf, size);
}

char *getwd(char *buf) {
	return getcwd(buf, 256);
}

int getuid() {
	return syscall_getuid();
}

int getgid() {
	return getuid();
}

int getpgrp() {
	/* XXX */
	return getgid();
}

int geteuid() {
	return getuid();
}

int getegid() {
	return getgid();
}

int pipe(int fildes[2]) {
	int ret = syscall_pipe((int *)fildes);
	if (ret < 0) {
		errno = -ret;
		return -1;
	}
	return ret;
}

ssize_t readlink(const char * name, char * buf, size_t len) {
	int r = syscall_readlink((char*)name, buf, len);

	if (r < 0) {
		errno = -r;
		return -1;
	}

	return r;
}

int usleep(useconds_t usec) {
	syscall_nanosleep(0, usec / 10000);
	return 0;
}

int fstat(int file, struct stat *st) {
	syscall_fstat(file, st);
	return 0;
}
