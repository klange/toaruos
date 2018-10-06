#include <syscall.h>
#include <syscall_nums.h>
#include <sys/ioctl.h>
#include <pty.h>
#include <errno.h>

DEFN_SYSCALL5(openpty, SYS_OPENPTY, int *, int *, char *, void *, void *);

int openpty(int * amaster, int * aslave, char * name, const struct termios *termp, const struct winsize * winp) {
	__sets_errno(syscall_openpty(amaster,aslave,name,(struct termios *)termp,(struct winsize *)winp));
}
