#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <syscall.h>
#include <bits/dirent.h>

DIR * opendir (const char * dirname) {
	int fd = open(dirname, O_RDONLY);
	if (fd == -1) {
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
		return -1;
	}
}

struct dirent * readdir (DIR * dirp) {
	static struct dirent ent;

	int ret = syscall_readdir(dirp->fd, ++dirp->cur_entry, &ent);
	if (ret != 0) {
		memset(&ent, 0, sizeof(struct dirent));
		return NULL;
	}

	return &ent;
}
