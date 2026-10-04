/**
 * @brief Test SIGSYS.
 *
 * We raise a SIGSYS if a system call is made from outside of the interpreter code.
 * We include a siginfo payload similar to the one Linux provides when user syscall
 * dispatch is enabled, which has the syscall number (which is important because the
 * @c ucontext_t provided to the @p sa_sigaction function will have the system call
 * number register set to -EFAULT). The signal handler can then examine the system
 * call arguments using the macros in <sys/uregs.h> on the provided context, as well
 * as modify them as needed to emulate the system call or possibly pass it forward
 * to the libc's real implementation of the system call.
 */
#include <stdio.h>
#include <signal.h>
#include <sys/uregs.h>

#include <sys/syscall.h>
#include "../libc/syscall.h"

DEFN_SYSCALL0(getpid, SYS_GETPID);

void sig_action(int sig, siginfo_t * info, void * _ctx) {
	dprintf(2, "info->si_code = %d\n", info->si_code);
	dprintf(2, "info->si_call_addr = %p\n", info->si_call_addr);
	dprintf(2, "info->si_syscall = %d (getpid = %d)\n", info->si_syscall, SYS_GETPID);

	ucontext_t * ctx = _ctx;
	dprintf(2, "current return value is %d, rewriting to 42\n", uregs_syscall_result(&ctx->uc_mcontext.mc_regs));
	uregs_syscall_result(&ctx->uc_mcontext.mc_regs) = 42;
}

int main(int argc, char * argv[]) {
	struct sigaction sa = {0};
	sa.sa_sigaction = sig_action;
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGSYS, &sa, NULL);

	int val = syscall_getpid();
	dprintf(2, "syscall_getpid() = %d\n", val);

	return 0;
}
