#include <stdio.h>
#include <syscall.h>
#include <syscall_nums.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

DEFN_SYSCALL3(execve, SYS_EXECVE, char *, char **, char **);

extern char ** environ;
extern char * _argv_0;
extern int __libc_debug;

#define DEFAULT_PATH "/bin:/usr/bin"

int execve(const char *name, char * const argv[], char * const envp[]) {
	if (__libc_debug) {
		fprintf(stderr, "%s: execve(\"%s\"", _argv_0, name);
		char * const* arg = argv;
		while (*arg) {
			fprintf(stderr,", \"%s\"", *arg);
			arg++;
		}
		fprintf(stderr, ")\n");
	}
	__sets_errno(syscall_execve((char*)name,(char**)argv,(char**)envp));
}

int execvp(const char *file, char *const argv[]) {
	if (file && (!strstr(file, "/"))) {
		/* We don't quite understand "$PATH", so... */
		char * path = getenv("PATH");
		if (!path) {
			path = DEFAULT_PATH;
		}
		char * xpath = strdup(path);
		char * p, * last;
		for ((p = strtok_r(xpath, ":", &last)); p; p = strtok_r(NULL, ":", &last)) {
			int r;
			struct stat stat_buf;
			char * exe = malloc(strlen(p) + strlen(file) + 2);
			strcpy(exe, p);
			strcat(exe, "/");
			strcat(exe, file);

			r = stat(exe, &stat_buf);
			if (r != 0) {
				continue;
			}
			if (!(stat_buf.st_mode & 0111)) {
				continue; /* XXX not technically correct; need to test perms */
			}

			return execve(exe, argv, environ);
		}
		free(xpath);
		errno = ENOENT;
		return -1;
	} else if (file) {
		return execve(file, argv, environ);
	}
	errno = ENOENT;
	return -1;
}

int execv(const char * file, char * const argv[]) {
	return execve(file, argv, environ);
}
