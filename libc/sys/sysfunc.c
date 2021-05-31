#include <errno.h>
#include <syscall.h>
#include <syscall_nums.h>
#include <sys/sysfunc.h>

DEFN_SYSCALL2(sysfunc, SYS_SYSFUNC, int, char **);

extern int sysfunc(int command, char ** args) {
	__sets_errno(syscall_sysfunc(command, args));
}
