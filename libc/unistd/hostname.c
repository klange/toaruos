#include <unistd.h>
#include <syscall.h>
#include <syscall_nums.h>
#include <errno.h>

DEFN_SYSCALL1(sethostname, SYS_SETHOSTNAME, char *);
DEFN_SYSCALL1(gethostname, SYS_GETHOSTNAME, char *);

int gethostname(char * name, size_t len) {
	(void)len; /* TODO */
	__sets_errno(syscall_gethostname(name));
}

int sethostname(const char * name, size_t len) {
	(void)len; /* TODO */
	__sets_errno(syscall_sethostname((char*)name));
}
