#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <syscall.h>
#include <syscall_nums.h>

DEFN_SYSCALL2(getcwd, SYS_GETCWD, char *, size_t);

char *getcwd(char *buf, size_t size) {
	if (!buf) buf = malloc(size);
	long result = syscall_getcwd(buf, size);
	if (result < 0) {
		errno = -result;
		return NULL;
	}
	return buf;
}

