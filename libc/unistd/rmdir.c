#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

int rmdir(const char *pathname) {
	/* XXX: This is subject to TOCTOU issues, but whatever. */
	struct stat st;

	/* pathname must directly name a directory, not a symlink */
	if (lstat(pathname, &st) < 0) return -1;
	if (!S_ISDIR(st.st_mode)) {
		errno = ENOTDIR;
		return -1;
	}

	/* our unlink can remove empty directories */
	return unlink(pathname);
}
