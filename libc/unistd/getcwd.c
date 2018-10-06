#include <unistd.h>
#include <string.h>
#include <syscall.h>

DEFN_SYSCALL2(getcwd, 29, char *, size_t);

char *getcwd(char *buf, size_t size) {
	if (!buf) buf = malloc(size);
	return (char *)syscall_getcwd(buf, size);
}

