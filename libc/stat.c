#include <errno.h>
#include <syscall.h>
#include <sys/stat.h>
#include <string.h>

#ifndef syscall_lstat
DECL_SYSCALL2(lstat, char *, void *);
#endif

int stat(const char *file, struct stat *st){
	int ret = syscall_stat((char *)file, (void *)st);
	if (ret >= 0) {
		return ret;
	} else {
		errno = ENOENT; /* meh */
		memset(st, 0x00, sizeof(struct stat));
		return ret;;
	}
}

int lstat(const char *path, struct stat *st) {
	int ret = syscall_lstat((char *)path, (void *)st);
	if (ret >= 0) {
		return ret;
	} else {
		errno = -ret;
		memset(st, 0x00, sizeof(struct stat));
		return ret;
	}
}
