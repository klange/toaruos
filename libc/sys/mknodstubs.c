#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

int mknodat(int f, const char* p, mode_t m, dev_t d) {
	if (geteuid() != 0) return errno = EPERM, -1;
	if ((m & S_IFMT) != S_IFIFO) return errno = EINVAL, -1;
	return errno = ENOTSUP, -1;
}

int mknod(const char* p, mode_t m, dev_t d) {
	return mknodat(AT_FDCWD, p, m, d);
}

int mkfifoat(int f, const char* p, mode_t m) {
	return errno = ENOTSUP, -1;
}

int mkfifo(const char* p, mode_t m) {
	return mkfifoat(AT_FDCWD, p, m);
}

