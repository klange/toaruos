/**
 * @brief 32-bit stat family binary compatibility.
 *
 * These functions provide the symbols 'stat', 'lstat', and 'fstat' for
 * old binaries that expect the 32-bit version of 'struct stat'. New code
 * that has been compiled with newer headers will instead look for '__*statns'
 * which has 64-bit dev and ino members as well as the titular nanosecond
 * precision time members.
 */
#include <string.h>
#include <sys/stat.h>
#include <_cheader.h>

struct stat_compat  {
	int  st_dev;
	int  st_ino;
	mode_t  st_mode;
	nlink_t  st_nlink;
	uid_t  st_uid;
	gid_t  st_gid;
	dev_t  st_rdev;
	off_t  st_size;
	time_t __st_atime;
	time_t __st_mtime;
	time_t __st_ctime;
	blksize_t  st_blksize;
	blkcnt_t  st_blocks;
};

static void convert(const struct stat *nst, struct stat_compat *ost) {
	ost->st_dev = nst->st_dev;
	ost->st_ino = nst->st_ino;
	ost->st_mode = nst->st_mode;
	ost->st_nlink = nst->st_nlink;
	ost->st_uid = nst->st_uid;
	ost->st_gid = nst->st_gid;
	ost->st_rdev = nst->st_rdev;
	ost->st_size = nst->st_size;

	ost->__st_atime = nst->st_atim.tv_sec;
	ost->__st_mtime = nst->st_mtim.tv_sec;
	ost->__st_ctime = nst->st_ctim.tv_sec;
	ost->st_blksize = nst->st_blksize;
	ost->st_blocks = nst->st_blocks;
}

/* Need to have these declarations for the redirects to work. */
int __stat_compat(const char *, struct stat_compat *);
int __lstat_compat(const char *, struct stat_compat *);
int __fstat_compat(int, struct stat_compat *);

__redirect(__stat_compat,stat);
__redirect(__lstat_compat,lstat);
__redirect(__fstat_compat,fstat);

int __stat_compat(const char *path, struct stat_compat *st) {
	struct stat nst;
	int ret = stat(path, &nst);
	if (ret < 0) return ret;
	convert(&nst,st);
	return ret;
}

int __lstat_compat(const char *path, struct stat_compat *st) {
	struct stat nst;
	int ret = lstat(path, &nst);
	if (ret < 0) return ret;
	convert(&nst,st);
	return ret;
}

int __fstat_compat(int fd, struct stat_compat *st) {
	struct stat nst;
	int ret = fstat(fd, &nst);
	if (ret < 0) return ret;
	convert(&nst,st);
	return ret;
}
