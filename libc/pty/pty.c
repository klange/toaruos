#include <libc/syscall.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <pty.h>
#include <errno.h>

DEFN_SYSCALL5(openpty, SYS_OPENPTY, int *, int *, char *, void *, void *);

int openpty(int * amanager, int * asubsidiary, char * name, const struct termios *termp, const struct winsize * winp) {
	__sets_errno(syscall_openpty(amanager,asubsidiary,name,(struct termios *)termp,(struct winsize *)winp));
}
