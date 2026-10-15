#include <libc/syscall.h>
#include <sys/syscall.h>
#include <poll.h>
#include <errno.h>

DEFN_SYSCALL3(poll, SYS_POLL, struct pollfd *, unsigned int, int);

int poll(struct pollfd *fds, nfds_t nfds, int timeout) {
	__sets_errno(syscall_poll(fds, nfds, timeout));
}
