#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <syscall.h>
#include <syscall_nums.h>
#include <errno.h>
#include <bits/dirent.h>

DEFN_SYSCALL3(readdir, SYS_READDIR, int, int, void *);

DIR * opendir (const char * dirname) {
	int fd = open(dirname, O_RDONLY|O_DIRECTORY);
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

long telldir(DIR * dirp) {
	return (long)dirp->cur_entry;
}

void rewinddir(DIR * dirp) {
	dirp->cur_entry = -1;
}

void seekdir(DIR * dirp, long loc) {
	dirp->cur_entry = loc;
}

struct dirent32 {
	unsigned int d_ino;
	char d_name[256];
};

struct dirent32 * readdir32 (DIR * dirp) {
	static struct dirent32 ent;
	struct dirent* big = readdir(dirp);
	if (!big) return NULL;

	ent.d_ino = big->d_ino;
	memcpy(ent.d_name,big->d_name,sizeof(ent.d_name));
	return &ent;
}

struct dirent32 * readdir32 (DIR * dirp) __asm__("readdir");

int scandir(const char *dirname, struct dirent ***namelist, int (*select)(const struct dirent *), int (*compar)(const struct dirent **, const struct dirent **)) {

	DIR * dirfd = opendir(dirname);
	if (!dirfd) return -1;

	int avail = 4;
	int count = 0;
	struct dirent ** names = malloc(sizeof(struct dirent *) * avail);

	while (1) {
		struct dirent * ent = readdir(dirfd);
		if (!ent) break;

		if (!select || select(ent)) {
			if (count + 1 == avail) {
				avail *= 2;
				names = realloc(names, sizeof(struct dirent *) * avail);
			}

			names[count] = malloc(sizeof(struct dirent));
			memcpy(names[count], ent, sizeof(struct dirent));
			count++;
		}
	}

	closedir(dirfd);

	if (compar) {
		qsort(names, count, sizeof(struct dirent *), (int (*)(const void*,const void*))compar);
	}

	*namelist = names;

	return count;
}

int alphasort(const struct dirent ** c1, const struct dirent ** c2) {
	return strcmp((*c1)->d_name, (*c2)->d_name);
}
