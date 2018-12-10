#include <sys/utsname.h>
#include <syscall.h>
#include <syscall_nums.h>

DEFN_SYSCALL1(uname, SYS_UNAME, void *);

int uname(struct utsname *__name) {
	return syscall_uname((void *)__name);
}
