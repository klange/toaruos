/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Copyright 2012 Kevin Lange
 *
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <sys/times.h>
#include <sys/errno.h>
#include <sys/time.h>
#include <stdio.h>
#include <utime.h>

#include <_ansi.h>
#include <errno.h>

#define DECL_SYSCALL0(fn)                int syscall_##fn()
#define DECL_SYSCALL1(fn,p1)             int syscall_##fn(p1)
#define DECL_SYSCALL2(fn,p1,p2)          int syscall_##fn(p1,p2)
#define DECL_SYSCALL3(fn,p1,p2,p3)       int syscall_##fn(p1,p2,p3)
#define DECL_SYSCALL4(fn,p1,p2,p3,p4)    int syscall_##fn(p1,p2,p3,p4)
#define DECL_SYSCALL5(fn,p1,p2,p3,p4,p5) int syscall_##fn(p1,p2,p3,p4,p5)

#define DEFN_SYSCALL0(fn, num) \
	int syscall_##fn() { \
		int a; __asm__ __volatile__("int $0x7F" : "=a" (a) : "0" (num)); \
		return a; \
	}

#define DEFN_SYSCALL1(fn, num, P1) \
	int syscall_##fn(P1 p1) { \
		int a; __asm__ __volatile__("int $0x7F" : "=a" (a) : "0" (num), "b" ((int)p1)); \
		return a; \
	}

#define DEFN_SYSCALL2(fn, num, P1, P2) \
	int syscall_##fn(P1 p1, P2 p2) { \
		int a; __asm__ __volatile__("int $0x7F" : "=a" (a) : "0" (num), "b" ((int)p1), "c" ((int)p2)); \
		return a; \
	}

#define DEFN_SYSCALL3(fn, num, P1, P2, P3) \
	int syscall_##fn(P1 p1, P2 p2, P3 p3) { \
		int a; __asm__ __volatile__("int $0x7F" : "=a" (a) : "0" (num), "b" ((int)p1), "c" ((int)p2), "d" ((int)p3)); \
		return a; \
	}

#define DEFN_SYSCALL4(fn, num, P1, P2, P3, P4) \
	int syscall_##fn(P1 p1, P2 p2, P3 p3, P4 p4) { \
		int a; __asm__ __volatile__("int $0x7F" : "=a" (a) : "0" (num), "b" ((int)p1), "c" ((int)p2), "d" ((int)p3), "S" ((int)p4)); \
		return a; \
	}

#define DEFN_SYSCALL5(fn, num, P1, P2, P3, P4, P5) \
	int syscall_##fn(P1 p1, P2 p2, P3 p3, P4 p4, P5 p5) { \
		int a; __asm__ __volatile__("int $0x7F" : "=a" (a) : "0" (num), "b" ((int)p1), "c" ((int)p2), "d" ((int)p3), "S" ((int)p4), "D" ((int)p5)); \
		return a; \
	}

/* Core */
DECL_SYSCALL1(exit, int);
DECL_SYSCALL1(print, const char *);

/* Files */
DECL_SYSCALL1(close,  int);
DECL_SYSCALL3(open,   const char *, int, int);
DECL_SYSCALL3(read,   int, char *, int);
DECL_SYSCALL3(write,  int, char *, int);
DECL_SYSCALL2(fstat,  int, void *);
DECL_SYSCALL1(isatty, int);
DECL_SYSCALL2(link,   char *, char *);
DECL_SYSCALL1(unlink, char *);
DECL_SYSCALL3(lseek,  int, int, int);
DECL_SYSCALL2(stat,   const char *, void *);

/* Process Control */
DECL_SYSCALL0(getpid);
DECL_SYSCALL3(execve, char *, char **, char **);
DECL_SYSCALL0(fork);
DECL_SYSCALL2(kill, int, int);
DECL_SYSCALL1(wait, int *);

/* Memory management */
DECL_SYSCALL1(sbrk, int);

DECL_SYSCALL2(gettimeofday, void *, void *);

DECL_SYSCALL2(getcwd, char *, size_t);
DECL_SYSCALL1(chdir, char *);
DECL_SYSCALL2(mkdir, char *, unsigned int);
DECL_SYSCALL0(getuid);

DEFN_SYSCALL1(exit,  0, int)
DEFN_SYSCALL1(print, 1, const char *)
DEFN_SYSCALL3(open,  2, const char *, int, int)
DEFN_SYSCALL3(read,  3, int, char *, int)
DEFN_SYSCALL3(write, 4, int, char *, int)
DEFN_SYSCALL1(close, 5, int)
DEFN_SYSCALL2(gettimeofday, 6, void *, void *)
DEFN_SYSCALL3(execve, 7, char *, char **, char **)
DEFN_SYSCALL0(fork, 8)
DEFN_SYSCALL0(getpid, 9)
DEFN_SYSCALL1(sbrk, 10, int)
DEFN_SYSCALL3(lseek, 14, int, int, int);
DEFN_SYSCALL2(fstat, 15, int, void *);
DEFN_SYSCALL1(wait, 17, int *);
DEFN_SYSCALL0(mkpipe, 21);
DEFN_SYSCALL2(dup2, 22, int, int);
DEFN_SYSCALL0(getuid, 23);

DEFN_SYSCALL2(getcwd, 29, char *, size_t);
DEFN_SYSCALL1(chdir, 28, char *);
DEFN_SYSCALL2(mkdir, 34, char *, unsigned int);




// --- Process Control ---

int _exit(int val){
	return syscall_exit(val);
}

int execve(char *name, char **argv, char **env) {
	return syscall_execve(name,argv,env);
}

/*
 * getpid -- only one process, so just return 1.
 */
int getpid() {
	return syscall_getpid();
}

/* Fork. Duh. */
int fork(void) {
	return syscall_fork();
}


/*
 * kill -- go out via exit...
 */
int kill(int pid, int sig) {
	if(pid == getpid())
		_exit(sig);

	errno = EINVAL;
	return -1;
}

int wait(int *status) {
	fprintf(stderr, "[debug] pid %d: wait(**)\n", getpid());
	errno = ECHILD;
	return -1;
}

int waitpid(int pid, int *status, int options) {
	/* XXX: status, options? */
	return syscall_wait((int *)pid);
}

// --- I/O ---

/*
 * isatty -- returns 1 if connected to a terminal device,
 *           returns 0 if not. Since we're hooked up to a
 *           serial port, we'll say yes and return a 1.
 */
int isatty(int fd) {
	fprintf(stderr, "[debug] pid %d: isatty(%d);\n", getpid(), fd);

	return (fd < 3);
}


int close(int file) {
	return syscall_close(file);
}

int link(char *old, char *new) {
	fprintf(stderr, "[debug] pid %d: link(%s, %s);\n", getpid(), old, new);
	errno = EMLINK;
	return -1;
}

int lseek(int file, int ptr, int dir) {
	return syscall_lseek(file,ptr,dir);
}

int open(const char *name, int flags, ...) {
	return syscall_open(name,flags, 0);
}

int read(int file, char *ptr, int len) {
	return syscall_read(file,ptr,len);
}

int creat(const char *path, mode_t mode) {
	return open(path, O_WRONLY|O_CREAT|O_TRUNC, mode);
}

int fstat(int file, struct stat *st) {
	syscall_fstat(file, st);
	return 0;
}

int stat(const char *file, struct stat *st){
	int i = open(file, 0);
	int ret = fstat(i, st);
	close(i);
	return ret;
}

int unlink(char *name) {
	fprintf(stderr, "[debug] pid %d unlink(%s);\n", getpid(), name);
	errno = ENOENT;
	return -1;
}

int write(int file, char *ptr, int len) {
	return syscall_write(file,ptr,len);
}

// --- Memory ---

/* _end is set in the linker command file */
extern caddr_t _end;

#if 0
#define PAGE_SIZE 4096UL
#define PAGE_MASK 0xFFFFF000UL
#define HEAP_ADDR (((unsigned long long)&_end + PAGE_SIZE) & PAGE_MASK)
#endif

/*
 * sbrk: request a larger heap
 * [the kernel will give this to us]
 */
caddr_t sbrk(int nbytes){
	return (caddr_t)syscall_sbrk(nbytes);
}

// --- Other ---
int gettimeofday(struct timeval *p, void *z){
	return syscall_gettimeofday(p,z);
}

int pipe(int fildes[2]) {
	int fd = syscall_mkpipe();
	fildes[0] = fd;
	fildes[1] = fd;
	return 0;
}

int  fcntl(int fd, int cmd, ...) {
	if (cmd == F_GETFD || cmd == F_SETFD) {
		return 0;
	}
	fprintf(stderr, "[user/debug] Unsupported operation [fcntl]\n");
	/* Not supported */
	return -1;
}

mode_t umask(mode_t mask) {
	fprintf(stderr, "[user/debug] Unsupported operation [umask]\n");
	/* Not supported */
	return 0;
}

char *getwd(char *buf) {
	return syscall_getcwd(buf, 256);
}

int chmod(const char *path, mode_t mode) {
	fprintf(stderr, "[user/debug] Unsupported operation [chmod]\n");
	/* Not supported */
	return -1;
}

int access(const char *pathname, int mode) {
	fprintf(stderr, "[user/debug] Unsupported operation [access]\n");
	/* Not supported */
	return -1;
}

int lstat(const char *path, struct stat *buf) {
	return stat(path, buf);
}

long pathconf(char *path, int name) {
	fprintf(stderr, "[user/debug] Unsupported operation [pathconf]\n");
	/* Not supported */
	return -1;
}


int utime(const char *filename, const struct utimbuf *times) {
	fprintf(stderr, "[user/debug] Unsupported operation [utime]\n");
	return -1;
}

int chown(const char *path, uid_t owner, gid_t group) {
	fprintf(stderr, "[user/debug] Unsupported operation [chown]\n");
	return -1;
}

int rmdir(const char *pathname) {
	fprintf(stderr, "[user/debug] Unsupported operation [rmdir]\n");
	return -1;
}

int mkdir(const char *pathname, mode_t mode) {
	return syscall_mkdir(pathname, mode);
}

int chdir(const char *path) {
	return syscall_chdir(path);
}

char *ttyname(int fd) {
	errno = ENOTTY;
	return NULL;
}

unsigned int sleep(unsigned int seconds) {
	/* lol go fuck yourself */
	return 0;
}

char _username[256];
char *getlogin(void) {
#define LINE_LEN 4096
	FILE * passwd = fopen("/etc/passwd", "r");
	char line[LINE_LEN];
	
	int uid = syscall_getuid();

	while (fgets(line, LINE_LEN, passwd) != NULL) {

		line[strlen(line)-1] = '\0';

		char *p, *tokens[10], *last;
		int i = 0;
		for ((p = strtok_r(line, ":", &last)); p;
				(p = strtok_r(NULL, ":", &last)), i++) {
			if (i < 511) tokens[i] = p;
		}
		tokens[i] = NULL;

		if (atoi(tokens[2]) == uid) {
			memcpy(_username, tokens[0], strlen(tokens[0])+1);
			break;
		}
	}
	fclose(passwd);

	return &_username;
}

