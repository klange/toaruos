#include <unistd.h>
#include <libc/syscall.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <pty.h>
#include <errno.h>

DEFN_SYSCALL5(openpty, SYS_OPENPTY, int *, int *, char *, void *, void *);

int openpty(int * amanager, int * asubsidiary, char * name, const struct termios *termp, const struct winsize * winp) {
	__sets_errno(syscall_openpty(amanager,asubsidiary,name,(struct termios *)termp,(struct winsize *)winp));
}

int login_tty(int fd) {
	setsid();

	if (ioctl(fd, TIOCSCTTY, 0) < 0) return -1;

	dup2(fd, 0);
	dup2(fd, 1);
	dup2(fd, 2);

	if (fd > 2) close(fd);

	return 0;
}

pid_t forkpty(int * amanager, char * name, const struct termios *termp, const struct winsize * winp) {
	int subsidiary = -1;
	if (openpty(amanager, &subsidiary, name, termp, winp) < 0) return -1;
	pid_t out = fork();
	if (out == 0) {
		close(*amanager);
		login_tty(subsidiary);
	} else {
		close(subsidiary);
	}
	return out;
}
