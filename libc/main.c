#include <stdint.h>
#include <stddef.h>

#include <syscall.h>
#include <syscall_nums.h>

DEFN_SYSCALL1(exit,  0, int);
DEFN_SYSCALL1(print, 1, const char *);
DEFN_SYSCALL2(gettimeofday, 6, void *, void *);
DEFN_SYSCALL3(execve, 7, char *, char **, char **);
DEFN_SYSCALL1(sbrk, 10, int);
DEFN_SYSCALL0(getgraphicsaddress, 11);
DEFN_SYSCALL5(openpty, 13, int *, int *, char *, void *, void *);
DEFN_SYSCALL1(setgraphicsoffset, 16, int);
DEFN_SYSCALL1(wait, 17, unsigned int);
DEFN_SYSCALL0(getgraphicswidth,  18);
DEFN_SYSCALL0(getgraphicsheight, 19);
DEFN_SYSCALL0(getgraphicsdepth,  20);
DEFN_SYSCALL0(mkpipe, 21);
DEFN_SYSCALL1(setuid, 24, unsigned int);
DEFN_SYSCALL1(kernel_string_XXX, 25, char *);
DEFN_SYSCALL0(reboot, 26);
DEFN_SYSCALL3(readdir, 27, int, int, void *);
DEFN_SYSCALL3(clone, 30, uintptr_t, uintptr_t, void *);
DEFN_SYSCALL1(sethostname, 31, char *);
DEFN_SYSCALL1(gethostname, 32, char *);
DEFN_SYSCALL0(mousedevice, 33);
DEFN_SYSCALL2(mkdir, 34, char *, unsigned int);
DEFN_SYSCALL2(shm_obtain, 35, char *, size_t *);
DEFN_SYSCALL1(shm_release, 36, char *);
DEFN_SYSCALL2(share_fd, 39, int, int);
DEFN_SYSCALL1(get_fd, 40, int);
DEFN_SYSCALL0(gettid, 41);
DEFN_SYSCALL0(yield, 42);
DEFN_SYSCALL2(system_function, 43, int, char **);
DEFN_SYSCALL1(open_serial, 44, int);
DEFN_SYSCALL2(sleepabs,  45, unsigned long, unsigned long);
DEFN_SYSCALL3(ioctl, 47, int, int, void *);
DEFN_SYSCALL2(access, 48, char *, int);
DEFN_SYSCALL2(stat, 49, char *, void *);
DEFN_SYSCALL2(chmod, 50, char *, int);
DEFN_SYSCALL1(umask, 51, int);
DEFN_SYSCALL1(unlink, 52, char *);
DEFN_SYSCALL3(waitpid, 53, int, int *, int);
DEFN_SYSCALL5(mount, SYS_MOUNT, char *, char *, char *, unsigned long, void *);
DEFN_SYSCALL2(symlink, SYS_SYMLINK, char *, char *);
DEFN_SYSCALL2(lstat, SYS_LSTAT, char *, void *);
DEFN_SYSCALL2(fswait, SYS_FSWAIT, int, int *);
DEFN_SYSCALL3(fswait2, SYS_FSWAIT2, int, int *,int);
DEFN_SYSCALL3(chown, SYS_CHOWN, char *, int, int);

extern void _init();
extern void _fini();

char ** environ = NULL;
int _environ_size = 0;
char * _argv_0 = NULL;

char ** __argv = NULL;
extern char ** __get_argv(void) {
	return __argv;
}

extern void __stdio_init_buffers(void);

void _exit(int val){
	_fini();
	syscall_exit(val);

	__builtin_unreachable();
}

__attribute__((constructor))
static void _libc_init(void) {
	__stdio_init_buffers();

	unsigned int x = 0;
	unsigned int nulls = 0;
	for (x = 0; 1; ++x) {
		if (!__get_argv()[x]) {
			++nulls;
			if (nulls == 2) {
				break;
			}
			continue;
		}
		if (nulls == 1) {
			environ = &__get_argv()[x];
			break;
		}
	}
	_argv_0 = __get_argv()[0];
}

void pre_main(int (*main)(int,char**), int argc, char * argv[]) {
	if (!__get_argv()) {
		__argv = argv;
		_libc_init();
	}
	_init();
	_exit(main(argc, argv));
}

