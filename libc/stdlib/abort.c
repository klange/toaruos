#include <stdlib.h>
#include <syscall.h>
#include <signal.h>

void abort(void) {

	/* POSIX says this should start as a normal raising of SIGABRT, allowing a handler
	 * to potentially run. If the handler returns, or if we were ignoring or blocking
	 * SIGABRT, we can try to be more persuasive. */
	raise(SIGABRT);

	/* Reset to default, make sure it's not blocked, and try raising again. */
	sigaction(SIGABRT, &(struct sigaction){.sa_handler = SIG_DFL}, NULL);
	sigset_t abrt;
	sigemptyset(&abrt);
	sigaddset(&abrt, SIGABRT);
	sigprocmask(SIG_UNBLOCK, &abrt, NULL);
	raise(SIGABRT);

	/* At this point, we can do whatever we want to have an abnormal process termination,
	 * so let's try SIGDIAF: 1) It's toaru-specific, so it's unlikely anyone's masked it
	 * or installed a handler for it. 2) it defaults to kill. */
	raise(SIGDIAF);

	/* If that doesn't work, try KILL */
	raise(SIGKILL);

	/* And if all else fails, give up and exit more normally. */
	syscall_exit(-1);
	__builtin_unreachable();
}
