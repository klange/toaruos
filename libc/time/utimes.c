#include <sys/syscall.h>
#include <libc/syscall.h>
#include <sys/time.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

DEFN_SYSCALL2(futimens, SYS_FUTIMENS, int, const struct timespec *);
DEFN_SYSCALL4(utimensat, SYS_UTIMENSAT, int, const char *, const struct timespec *, int);

int futimes(int fd, const struct timeval times[2]) {
	struct timespec times_s[2] = { { 0, UTIME_NOW }, { 0, UTIME_NOW } };
	if (times) {
		times_s[0].tv_sec  = times[0].tv_sec;
		times_s[0].tv_nsec = times[0].tv_usec * 1000;
		times_s[1].tv_sec  = times[1].tv_sec;
		times_s[1].tv_nsec = times[1].tv_usec * 1000;
	}
	__sets_errno(syscall_futimens(fd, times_s));
}

int utimes(const char *path, const struct timeval times[2]) {
	struct timespec times_s[2] = { { 0, UTIME_NOW }, { 0, UTIME_NOW } };
	if (times) {
		times_s[0].tv_sec  = times[0].tv_sec;
		times_s[0].tv_nsec = times[0].tv_usec * 1000;
		times_s[1].tv_sec  = times[1].tv_sec;
		times_s[1].tv_nsec = times[1].tv_usec * 1000;
	}
	__sets_errno(syscall_utimensat(AT_FDCWD, path, times_s, 0));
}

int futimens(int fd, const struct timespec times[2]) {
	__sets_errno(syscall_futimens(fd, times));
}

int utimensat(int fd, const char *path, const struct timespec times[2], int flag) {
	__sets_errno(syscall_utimensat(fd, path, times, flag));
}

