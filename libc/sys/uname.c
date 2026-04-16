#include <sys/utsname.h>
#include <syscall.h>
#include <syscall_nums.h>
#include <errno.h>

DEFN_SYSCALL1(uname, SYS_UNAME, struct utsname *);

int uname(struct utsname *__name) {
	__sets_errno(syscall_uname(__name));
}
