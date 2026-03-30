#include <stdio.h>
#include <syscall.h>
#include <syscall_nums.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

DEFN_SYSCALL3(execve, SYS_EXECVE, char *, char **, char **);

extern char ** environ;

#define DEFAULT_PATH "/bin:/usr/bin"

int execve(const char *name, char * const argv[], char * const envp[]) {
	__sets_errno(syscall_execve((char*)name,(char**)argv,(char**)envp));
}

int execvpe(const char *file, char *const argv[], char *const envp[]) {
	if (file && (!strstr(file, "/"))) {
		/* We don't quite understand "$PATH", so... */
		char * path = getenv("PATH") ?: DEFAULT_PATH;
		char * xpath = strdup(path);
		char * p, * last;
		for ((p = strtok_r(xpath, ":", &last)); p; p = strtok_r(NULL, ":", &last)) {
			char *exe;
			if (asprintf(&exe, "%s/%s", p, file) == -1) continue;
			if (access(exe, X_OK)) {
				free(exe);
				continue;
			}
			return execve(exe, argv, envp);
		}
		free(xpath);
		errno = ENOENT;
		return -1;
	} else if (file) {
		return execve(file, argv, envp);
	}
	errno = ENOENT;
	return -1;
}

int execvp(const char *file, char *const argv[]) {
	return execvpe(file, argv, environ);
}

int execv(const char * file, char * const argv[]) {
	return execve(file, argv, environ);
}

int execl(const char *path, const char *arg, ...) {
	int argc = 1; /* Count */
	va_list ap;

	/* Count */
	va_start(ap, arg);
	while (va_arg(ap, char *)) argc++;
	va_end(ap);

	/* Copy */
	char * argv[argc+1];
	va_start(ap, arg);
	argv[0] = (char*)arg;
	for (int i = 1; i <= argc; ++i) argv[i] = va_arg(ap, char*);
	va_end(ap);

	/* Exec */
	return execv(path, argv);
}

int execlp(const char *path, const char *arg, ...) {
	int argc = 1; /* Count */
	va_list ap;

	/* Count */
	va_start(ap, arg);
	while (va_arg(ap, char *)) argc++;
	va_end(ap);

	/* Copy */
	char * argv[argc+1];
	va_start(ap, arg);
	argv[0] = (char*)arg;
	for (int i = 1; i <= argc; ++i) argv[i] = va_arg(ap, char*);
	va_end(ap);

	/* Exec */
	return execvp(path, argv);
}

int execle(const char *path, const char *arg, ...) {
	int argc = 1; /* Count */
	va_list ap;

	/* Count */
	va_start(ap, arg);
	while (va_arg(ap, char *)) argc++;
	va_end(ap);

	/* Copy */
	char * argv[argc+1];
	va_start(ap, arg);
	argv[0] = (char*)arg;
	for (int i = 1; i <= argc; ++i) argv[i] = va_arg(ap, char*);

	char ** envp = va_arg(ap, char**);
	va_end(ap);

	/* Exec */
	return execve(path, argv, envp);
}
