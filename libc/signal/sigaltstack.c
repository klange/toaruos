#include <signal.h>
#include <libc/syscall.h>
#include <sys/syscall.h>
#include <errno.h>

DEFN_SYSCALL2(sigaltstack, SYS_SIGALTSTACK, const stack_t *, stack_t *);

int sigaltstack(const stack_t *__restrict ss, stack_t *__restrict oss) {
	__sets_errno(syscall_sigaltstack(ss, oss));
}
