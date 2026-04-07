#include <unistd.h>
#include <syscall.h>
#include <syscall_nums.h>
#include <errno.h>

DEFN_SYSCALL2(sethostname, SYS_SETHOSTNAME, char *, size_t);
DEFN_SYSCALL2(gethostname, SYS_GETHOSTNAME, char *, size_t);

int gethostname(char * name, size_t len) {
	__sets_errno(syscall_gethostname(name, len));
}

int sethostname(const char * name, size_t len) {
	__sets_errno(syscall_sethostname((char*)name, len));
}
