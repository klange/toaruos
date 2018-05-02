#include <sys/utsname.h>
#include <syscall.h>

DEFN_SYSCALL1(uname, 12, void *);

int uname(struct utsname *__name) {
	return syscall_uname((void *)__name);
}
