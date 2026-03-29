#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <syscall.h>
#include <syscall_nums.h>

DEFN_SYSCALL2(getcwd, SYS_GETCWD, char *, size_t);

char *getcwd(char *buf, size_t size) {

	/* If you want us to allocate exactly enough space, you can't provide a pointer. */
	if (buf && !size) {
		errno = -EINVAL;
		return NULL;
	}

	char * path = buf;
	size_t want = size;

	if (!size) want = PATH_MAX;
	if (!buf) path = malloc(want);

	long result = syscall_getcwd(path, want);

	if (result > 0 && !buf && !size) return realloc(path, result);

	if (result < 0) {
		errno = -result;
		if (!buf) free(path);
		return NULL;
	}

	return path;
}

