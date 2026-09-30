#include <fcntl.h>
#include <errno.h>
#include <libc/syscall.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <string.h>

DEFN_SYSCALL4(fstatat, SYS_FSTATAT, int, const char *, void *, int);

int fstatat(int dirfd, const char * path, struct stat *st, int flag) {
	int ret = syscall_fstatat(dirfd, path, st, flag);
	if (ret >= 0) return ret;
	errno = -ret;
	memset(st, 0, sizeof(struct stat));
	return -1;
}

int stat(const char * file, struct stat *st) {
	return fstatat(AT_FDCWD, file, st, 0);
}

int lstat(const char * file, struct stat *st) {
	return fstatat(AT_FDCWD, file, st, AT_SYMLINK_NOFOLLOW);
}
