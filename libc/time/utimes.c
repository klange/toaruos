#include <sys/syscall.h>
#include <libc/syscall.h>
#include <sys/time.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

DEFN_SYSCALL3(utimens, SYS_UTIMENS, const char *, const struct timespec *, const struct timespec *);
DEFN_SYSCALL3(futimens, SYS_FUTIMENS, int, const struct timespec *, const struct timespec *);

int futimes(int fd, const struct timeval times[2]) {
	struct timespec access;
	access.tv_sec  = times[0].tv_sec;
	access.tv_nsec = times[0].tv_usec * 1000;
	struct timespec modify;
	modify.tv_sec  = times[1].tv_sec;
	modify.tv_nsec = times[1].tv_usec * 1000;
	__sets_errno(syscall_futimens(fd, &access, &modify));
}

int utimes(const char *path, const struct timeval times[2]) {
	struct timespec access;
	access.tv_sec  = times[0].tv_sec;
	access.tv_nsec = times[0].tv_usec * 1000;
	struct timespec modify;
	modify.tv_sec  = times[1].tv_sec;
	modify.tv_nsec = times[1].tv_usec * 1000;
	__sets_errno(syscall_utimens(path, &access, &modify));
}

int futimens(int fd, const struct timespec times[2]) {
	__sets_errno(syscall_futimens(fd, &times[0], &times[1]));
}

int utimensat(int fd, const char *path, const struct timespec times[2], int flag) {
	if (fd != AT_FDCWD) return -ENOTSUP;
	if (flag != 0) return -ENOTSUP;
	__sets_errno(syscall_utimens(path, &times[0], &times[1]));
}

