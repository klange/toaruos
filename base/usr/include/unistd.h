#pragma once

#include <_cheader.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

_Begin_C_Header

extern char **environ;

extern pid_t getpid(void);
extern pid_t getppid(void);

extern int close(int fd);

extern pid_t fork(void);

extern int execl(const char *path, const char *arg, ...);
extern int execlp(const char *file, const char *arg, ...);
extern int execle(const char *path, const char *arg, ...);
extern int execv(const char *path, char *const argv[]);
extern int execvp(const char *file, char *const argv[]);
extern int execvpe(const char *file, char *const argv[], char *const envp[]);
extern int execve(const char *name, char * const argv[], char * const envp[]);
extern void _exit(int status);

extern int setuid(uid_t uid);

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

extern int symlink(const char *target, const char *linkpath);
extern ssize_t readlink(const char *pathname, char *buf, size_t bufsiz);

extern int chdir(const char *path);
//extern int fchdir(int fd);
extern int isatty(int fd);

extern unsigned int sleep(unsigned int seconds);
extern int usleep(useconds_t usec);
extern off_t lseek(int fd, off_t offset, int whence);

extern int access(const char * pathname, int mode);

extern int getopt(int argc, char * const argv[], const char * optstring);

extern char * optarg;
extern int optind, opterr, optopt;

extern int unlink(const char * pathname);

/* Unimplemented stubs */
struct utimbuf {
    time_t actime;
    time_t modtime;
};
extern char * ttyname(int fd);
extern int utime(const char *filename, const struct utimbuf *times);
extern int rmdir(const char *pathname); /* TODO  rm probably just works */
extern int chown(const char * pathname, uid_t owner, gid_t group);
extern char * getlogin(void);

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define F_OK 0
#define R_OK 4
#define W_OK 2
#define X_OK 1

extern int gethostname(char * name, size_t len);
extern int sethostname(const char * name, size_t len);

extern pid_t setsid(void);
extern int setpgid(pid_t, pid_t);
extern pid_t getpgid(pid_t);

extern unsigned int alarm(unsigned int seconds);

extern void *sbrk(intptr_t increment);

_End_C_Header
