#define _TOARU_SOURCE
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include "libc/pthread/internal.h"

int pthread_setname_np(pthread_t thread, const char * name) {
	if (strlen(name) > 15) return ERANGE;
	char path[] = "/proc/XXXXXXXXXXX/comm";
	snprintf(path, sizeof(path), "/proc/%d/comm", thread->tid);
	int fd = open(path, O_WRONLY | O_CLOEXEC);
	if (fd < 0) return -fd;
	ssize_t w = write(fd, name, strlen(name));
	close(fd);
	if (w < 0) return -w;
	return 0;
}

int pthread_getname_np(pthread_t thread, char * name, size_t len) {
	if (len < 16) return ERANGE;
	char path[] = "/proc/XXXXXXXXXXX/comm";
	snprintf(path, sizeof(path), "/proc/%d/comm", thread->tid);
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) return -fd;
	ssize_t r = read(fd, name, len);
	close(fd);
	if (r < 0) return -r;
	if (r > 0 && name[r-1] == '\n') name[r-1] = '\0';
	return 0;
}

