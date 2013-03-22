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
#include <sys/utsname.h>
#include <sys/termios.h>
#include <stdio.h>
#include <utime.h>
#include <signal.h>

#include <_ansi.h>
#include <errno.h>

#include "syscall.h"
#include <bits/dirent.h>

extern void _init();
extern void _fini();

DEFN_SYSCALL1(exit,  0, int);
DEFN_SYSCALL1(print, 1, const char *);
DEFN_SYSCALL3(open,  2, const char *, int, int);
DEFN_SYSCALL3(read,  3, int, char *, int);
DEFN_SYSCALL3(write, 4, int, char *, int);
DEFN_SYSCALL1(close, 5, int);
DEFN_SYSCALL2(gettimeofday, 6, void *, void *);
DEFN_SYSCALL3(execve, 7, char *, char **, char **);
DEFN_SYSCALL0(fork, 8);
DEFN_SYSCALL0(getpid, 9);
DEFN_SYSCALL1(sbrk, 10, int);
DEFN_SYSCALL0(getgraphicsaddress, 11);
DEFN_SYSCALL1(uname, 12, void *);
DEFN_SYSCALL5(openpty, 13, int *, int *, char *, void *, void *);
DEFN_SYSCALL3(lseek, 14, int, int, int);
DEFN_SYSCALL2(fstat, 15, int, void *);
DEFN_SYSCALL1(setgraphicsoffset, 16, int);
DEFN_SYSCALL1(wait, 17, unsigned int);
DEFN_SYSCALL0(getgraphicswidth,  18);
DEFN_SYSCALL0(getgraphicsheight, 19);
DEFN_SYSCALL0(getgraphicsdepth,  20);
DEFN_SYSCALL0(mkpipe, 21);
DEFN_SYSCALL2(dup2, 22, int, int);
DEFN_SYSCALL0(getuid, 23);
DEFN_SYSCALL1(setuid, 24, unsigned int);
DEFN_SYSCALL1(kernel_string_XXX, 25, char *);
DEFN_SYSCALL0(reboot, 26);
DEFN_SYSCALL3(readdir, 27, int, int, void *);
DEFN_SYSCALL1(chdir, 28, char *);
DEFN_SYSCALL2(getcwd, 29, char *, size_t);
DEFN_SYSCALL3(clone, 30, uintptr_t, uintptr_t, void *);
DEFN_SYSCALL1(sethostname, 31, char *);
DEFN_SYSCALL1(gethostname, 32, char *);
DEFN_SYSCALL0(mousedevice, 33);
DEFN_SYSCALL2(mkdir, 34, char *, unsigned int);
DEFN_SYSCALL2(shm_obtain, 35, char *, size_t *);
DEFN_SYSCALL1(shm_release, 36, char *);
DEFN_SYSCALL2(send_signal, 37, uint32_t, uint32_t);
DEFN_SYSCALL2(signal, 38, uint32_t, void *);
DEFN_SYSCALL2(share_fd, 39, int, int);
DEFN_SYSCALL1(get_fd, 40, int);
DEFN_SYSCALL0(gettid, 41);
DEFN_SYSCALL0(yield, 42);
DEFN_SYSCALL2(system_function, 43, int, char **);
DEFN_SYSCALL1(open_serial, 44, int);
DEFN_SYSCALL2(sleepabs,  45, unsigned long, unsigned long);
DEFN_SYSCALL2(nanosleep,  46, unsigned long, unsigned long);
DEFN_SYSCALL3(ioctl, 47, int, int, void *);
DEFN_SYSCALL2(access, 48, char *, int);

#define DEBUG_STUB(...) { char buf[512]; sprintf(buf, "\033[1;32mUserspace Debug\033[0m pid%d ", getpid()); syscall_print(buf); sprintf(buf, __VA_ARGS__); syscall_print(buf); }


extern char ** environ;

// --- Process Control ---

int _exit(int val){
	_fini();
	return syscall_exit(val);
}

int execve(char *name, char **argv, char **env) {
	return syscall_execve(name,argv,env);
}

int execvp(const char *file, char *const argv[]) {
	return syscall_execve(file,argv,environ);
}

int execv(const char * file, char *const argv[]) {
	DEBUG_STUB("execv(%s,...);\n", file);
	return syscall_execve(file,argv,environ);
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

int uname(struct utsname *__name) {
	return syscall_uname((void *)__name);
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

int waitpid(int pid, int *status, int options) {
	/* XXX: status, options? */
	int x  = syscall_wait(pid);
	if (status) *status = x;
	return x;
}

int wait(int *status) {
	return waitpid(0, status, 0);
}

// --- I/O ---

int isatty(int fd) {
	/* XXX: Do the right thing */
	return (fd < 3);
}


int close(int file) {
	return syscall_close(file);
}

int link(char *old, char *new) {
	DEBUG_STUB("[debug] pid %d: link(%s, %s);\n", getpid(), old, new);
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
	if (i >= 0) {
		int ret = fstat(i, st);
		close(i);
		return ret;
	} else {
		return -1;
	}
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

char *getwd(char *buf) {
	return syscall_getcwd(buf, 256);
}

char *getcwd(char *buf, size_t size) {
	return syscall_getcwd(buf, size);
}

int lstat(const char *path, struct stat *buf) {
	return stat(path, buf);
}

int mkdir(const char *pathname, mode_t mode) {
	return syscall_mkdir(pathname, mode);
}

int chdir(const char *path) {
	return syscall_chdir(path);
}

unsigned int sleep(unsigned int seconds) {
	syscall_nanosleep(seconds, 0);
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

int dup2(int oldfd, int newfd) {
	DEBUG_STUB("dup2(%d,%d);\n", oldfd, newfd);
	return syscall_dup2(oldfd, newfd);
}

DIR * opendir (const char * dirname) {
	int fd = open(dirname, O_RDONLY);
	if (fd == -1) {
		return NULL;
	}

	DIR * dir = malloc(sizeof(DIR));
	dir->fd = fd;
	dir->cur_entry = -1;
	return dir;
}

int closedir (DIR * dir) {
	if (dir && (dir->fd != -1)) {
		return close(dir->fd);
	} else {
		return -1;
	}
}

struct dirent * readdir (DIR * dirp) {
	static struct dirent ent;

	int ret = syscall_readdir(dirp->fd, ++dirp->cur_entry, &ent);
	if (ret != 0) {
		memset(&ent, 0, sizeof(struct dirent));
		return NULL;
	}

	return &ent;
}

void pre_main(int argc, char * argv[]) {
	unsigned int x = 0;
	unsigned int nulls = 0;
	for (x = 0; 1; ++x) {
		if (!argv[x]) {
			++nulls;
			if (nulls == 2) {
				break;
			}
			continue;
		}
		if (nulls == 1) {
			environ = &argv[x];
			break;
		}
	}
	_init();
	_exit(main(argc, argv));
}


/* XXX Unimplemented functions */
unsigned int alarm(unsigned int seconds) {
	DEBUG_STUB("alarm(%s);\n", seconds);
	return 0;
}

clock_t times(struct tms *buf) {
	/* TODO: times() */
	return -1;
}


int  fcntl(int fd, int cmd, ...) {
	if (cmd == F_GETFD || cmd == F_SETFD) {
		return 0;
	}
	DEBUG_STUB("[user/debug] Unsupported operation [fcntl]\n");
	/* Not supported */
	return -1;
}

mode_t umask(mode_t mask) {
	DEBUG_STUB("[user/debug] Unsupported operation [umask]\n");
	/* Not supported */
	return 0;
}

int chmod(const char *path, mode_t mode) {
	DEBUG_STUB("[user/debug] Unsupported operation [chmod]\n");
	/* Not supported */
	return -1;
}

int unlink(char *name) {
	DEBUG_STUB("[debug] pid %d unlink(%s);\n", getpid(), name);
	errno = ENOENT;
	return -1;
}

int access(const char *pathname, int mode) {
	return syscall_access((char *)pathname, mode);
}

long pathconf(char *path, int name) {
	DEBUG_STUB("[user/debug] Unsupported operation [pathconf]\n");
	/* Not supported */
	return 0;
}


int utime(const char *filename, const struct utimbuf *times) {
	DEBUG_STUB("[user/debug] Unsupported operation [utime]\n");
	return 0;
}

int chown(const char *path, uid_t owner, gid_t group) {
	DEBUG_STUB("[user/debug] Unsupported operation [chown]\n");
	return 0;
}

int rmdir(const char *pathname) {
	DEBUG_STUB("[user/debug] Unsupported operation [rmdir]\n");
	return 0;
}


char *ttyname(int fd) {
	errno = ENOTTY;
	return NULL;
}

long sysconf(int name) {
	switch (name) {
		case 8:
			return 4096;
		case 11:
			return 10000;
		default:
			DEBUG_STUB("sysconf(%d);\n", name);
			return 0;
	}
}

int ioctl(int fd, int request, void * argp) {
	return syscall_ioctl(fd, request, argp);
}

/* termios */
speed_t cfgetispeed(const struct termios * tio) {
	return 0;
}
speed_t cfgetospeed(const struct termios * tio) {
	return 0;
}

int cfsetispeed(struct termios * tio, speed_t speed) {
	/* hahahaha, yeah right */
	return 0;
}

int cfsetospeed(struct termios * tio, speed_t speed) {
	return 0;
}

int tcdrain(int i) {
	DEBUG_STUB("tcdrain(%d)\n", i);
	return 0;
}

int tcflow(int a, int b) {
	DEBUG_STUB("tcflow(%d,%d)\n", a, b);
	return 0;
}

int tcflush(int a, int b) {
	DEBUG_STUB("tcflow(%d,%d)\n", a, b);
	return 0;
}

int tcgetattr(int fd, struct termios * tio) {
	DEBUG_STUB("tcgetattr(%d, ...)\n", fd);
	return 0;
}

pid_t tcgetsid(int fd) {
	DEBUG_STUB("tcgetsid(%d)\n", fd);
	return getpid();
}

int tcsendbreak(int a, int b) {
	DEBUG_STUB("tcsendbreak(%d,%d)\n", a, b);
	return 0;
}

int tcsetattr(int fd, int actions, struct termios * tio) {
	DEBUG_STUB( "tcsetattr(%d,%d,...)\n", fd, actions);

	DEBUG_STUB( "   0x%8x\n", tio->c_cflag);
	DEBUG_STUB( "   0x%8x\n", tio->c_iflag);
	DEBUG_STUB( "   0x%8x\n", tio->c_lflag);
	DEBUG_STUB( "   0x%8x\n", tio->c_oflag);

	return 0;
}

int tcsetpgrp(int fd, pid_t pgrp) {
	DEBUG_STUB("tcsetpgrp(%d,%d);\n", fd, pgrp);
	return 0;
}

pid_t tcgetpgrp(int fd) {
	DEBUG_STUB("tcgetpgrp(%d);\n", fd);
	return -1;
}

int fpathconf(int file, int name) {
	DEBUG_STUB("fpathconf(%d,%d)\n", file, name);
	return 0;
}

int getuid() {
	return syscall_getuid();
}

int getgid() {
	return getuid();
}

int getpgrp() {
	/* XXX */
	return getgid();
}

int geteuid() {
	return getuid();
}

int getegid() {
	return getgid();
}

int getgroups(int size, gid_t list[]) {
	DEBUG_STUB("getgroups(...);\n");
	return 0;
}

pid_t wait3(int *status, int options, struct rusage *rusage) {
	return wait(status);
}

int dup(int oldfd) {
	return dup2(oldfd, 0);
}

int sigprocmask(int how, const sigset_t *set, sigset_t *oldset) {
	DEBUG_STUB("sigprocmask(%d, 0x%x, 0x%x);\n", how, set, oldset);
	return -1;
}

int sigsuspend(const sigset_t * mask) {
	DEBUG_STUB("sigsuspend(0x%x);\n", mask);
	syscall_yield();
	return -1;
}

int setpgid(pid_t pid, pid_t pgid) {
	DEBUG_STUB("setpgid(%d,%d);\n", pid, pgid);
	return -1;
}

int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact)  {
	DEBUG_STUB("sigaction(%d,...);\n", signum);

	return 0;
}

pid_t getppid() {
	DEBUG_STUB("getppid()\n");
	return 0;
}


void sync() {
	DEBUG_STUB("sync();\n");
}

