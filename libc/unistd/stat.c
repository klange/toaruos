#include <errno.h>
#include <syscall.h>
#include <syscall_nums.h>
#include <sys/stat.h>
#include <string.h>

DEFN_SYSCALL2(statf, SYS_STATF, char *, void *);
DEFN_SYSCALL2(lstat, SYS_LSTAT, char *, void *);

int stat(const char *file, struct stat *st){
	int ret = syscall_statf((char *)file, (void *)st);
	if (ret >= 0) {
		return ret;
	} else {
		errno = -ret;
		memset(st, 0x00, sizeof(struct stat));
		return -1;
	}
}

int lstat(const char *path, struct stat *st) {
	int ret = syscall_lstat((char *)path, (void *)st);
	if (ret >= 0) {
		return ret;
	} else {
		errno = -ret;
		memset(st, 0x00, sizeof(struct stat));
		return -1;
	}
}
