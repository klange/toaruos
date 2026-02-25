#include <string.h>
#include <sys/stat.h>

struct stat_compat  {
	dev_t  st_dev;
	ino_t  st_ino;
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
	memcpy(ost, nst, offsetof(struct stat_compat,__st_atime));
	ost->__st_atime = nst->st_atim.tv_sec;
	ost->__st_mtime = nst->st_mtim.tv_sec;
	ost->__st_ctime = nst->st_ctim.tv_sec;
	ost->st_blksize = nst->st_blksize;
	ost->st_blocks = nst->st_blocks;
}

int __stat_compat(const char *, struct stat_compat *) __asm__("stat");
int __stat_compat(const char *path, struct stat_compat *st) {
	struct stat nst;
	int ret = stat(path, &nst);
	if (ret < 0) return ret;
	convert(&nst,st);
	return ret;
}

int __lstat_compat(const char *, struct stat_compat *) __asm__("lstat");
int __lstat_compat(const char *path, struct stat_compat *st) {
	struct stat nst;
	int ret = lstat(path, &nst);
	if (ret < 0) return ret;
	convert(&nst,st);
	return ret;
}

int __fstat_compat(int, struct stat_compat *) __asm__("fstat");
int __fstat_compat(int fd, struct stat_compat *st) {
	struct stat nst;
	int ret = fstat(fd, &nst);
	if (ret < 0) return ret;
	convert(&nst,st);
	return ret;
}
