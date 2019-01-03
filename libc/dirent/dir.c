#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <syscall.h>
#include <syscall_nums.h>
#include <errno.h>
#include <bits/dirent.h>

DEFN_SYSCALL3(readdir, SYS_READDIR, int, int, void *);

DIR * opendir (const char * dirname) {
	int fd = open(dirname, O_RDONLY);
	if (fd < 0) {
		/* errno was set by open */
		return NULL;
	}

	DIR * dir = (DIR *)malloc(sizeof(DIR));
	dir->fd = fd;
	dir->cur_entry = -1;
	return dir;
}

int closedir (DIR * dir) {
	if (dir && (dir->fd != -1)) {
		return close(dir->fd);
	} else {
		return -EBADF;
	}
}

struct dirent * readdir (DIR * dirp) {
	static struct dirent ent;

	int ret = syscall_readdir(dirp->fd, ++dirp->cur_entry, &ent);
	if (ret < 0) {
		errno = -ret;
		memset(&ent, 0, sizeof(struct dirent));
		return NULL;
	}

	if (ret == 0) {
		/* end of directory */
		memset(&ent, 0, sizeof(struct dirent));
		return NULL;
	}

	return &ent;
}
