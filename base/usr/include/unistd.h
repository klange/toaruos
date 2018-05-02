#pragma once

#include <stddef.h>
#include <sys/types.h>

extern char **environ;

extern pid_t getpid(void);

extern int close(int fd);

extern pid_t fork(void);

extern int execl(const char *path, const char *arg, ...);
extern int execlp(const char *file, const char *arg, ...);
extern int execle(const char *path, const char *arg, ...);
extern int execv(const char *path, char *const argv[]);
extern int execvp(const char *file, char *const argv[]);
extern int execvpe(const char *file, char *const argv[], char *const envp[]);
extern void _exit(int status);

extern uid_t getuid(void);
extern uid_t geteuid(void);
extern gid_t getgid(void);
extern gid_t getegid(void);
extern char * getcwd(char *buf, size_t size);
extern int pipe(int pipefd[2]);
extern int dup(int oldfd);
extern int dup2(int oldfd, int newfd);

extern pid_t tcgetpgrp(int fd);
extern int tcsetpgrp(int fd, pid_t pgrp);

extern ssize_t write(int fd, const void * buf, size_t count);
extern ssize_t read(int fd, void * buf, size_t count);

extern ssize_t readlink(const char *pathname, char *buf, size_t bufsiz);

extern int chdir(const char *path);
extern int fchdir(int fd);
extern int isatty(int fd);

extern int usleep(useconds_t usec);
extern off_t lseek(int fd, off_t offset, int whence);

extern int access(const char * pathname, int mode);


#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
