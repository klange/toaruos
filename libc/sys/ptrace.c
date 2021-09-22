#include <syscall.h>
#include <syscall_nums.h>
#include <sys/ptrace.h>
#include <errno.h>

DEFN_SYSCALL4(ptrace, SYS_PTRACE, int, int, void *, void *);

long ptrace(enum __ptrace_request request, pid_t pid, void * addr, void * data) {
	__sets_errno(syscall_ptrace(request,pid,addr,data));
}
