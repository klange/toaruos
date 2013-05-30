/* sys/signal.h */

#ifndef _SYS_SIGNAL_H
#define _SYS_SIGNAL_H
#ifdef __cplusplus
extern "C" {
#endif

#include "_ansi.h"
#include <sys/features.h>
#include <sys/types.h>

typedef unsigned long sigset_t;

#define SIGEV_NONE   1
#define SIGEV_SIGNAL 2
#define SIGEV_THREAD 3

#define SI_USER    1
#define SI_QUEUE   2
#define SI_TIMER   3
#define SI_ASYNCIO 4
#define SI_MESGQ   5

#define SA_NOCLDSTOP 1
#define SA_SIGINFO   2

#define SIG_SETMASK 0
#define SIG_BLOCK 1
#define SIG_UNBLOCK 2

#define sa_handler   _signal_handlers._handler
#define sa_sigaction _signal_handlers._sigaction

union sigval {
	int    sival_int;
	void  *sival_ptr;
};

struct sigevent {
	int              sigev_notify;
	int              sigev_signo;
	union sigval     sigev_value;
};

typedef struct {
	int          si_signo;
	int          si_code;
	union sigval si_value;
} siginfo_t;

typedef void (*_sig_func_ptr)();

struct sigaction {
	int         sa_flags;
	sigset_t    sa_mask;
	union {
		_sig_func_ptr _handler;
		void      (*_sigaction)( int, siginfo_t *, void * );
	} _signal_handlers;
};

#define sigaddset(what,sig)   (*(what) |= (1<<(sig)), 0)
#define sigdelset(what,sig)   (*(what) &= ~(1<<(sig)), 0)
#define sigemptyset(what)     (*(what) = 0, 0)
#define sigfillset(what)      (*(what) = ~(0), 0)
#define sigismember(what,sig) (((*(what)) & (1<<(sig))) != 0)

int _EXFUN(kill, (pid_t, int));
int _EXFUN(killpg, (pid_t, int));
int _EXFUN(sigaction, (int, const struct sigaction *, struct sigaction *));
int _EXFUN(sigpending, (sigset_t *));
int _EXFUN(sigsuspend, (const sigset_t *));
int _EXFUN(sigpause, (int));


#include <sys/signal_defs.h>

#ifdef __cplusplus
}
#endif

#ifndef _SIGNAL_H_
#include <signal.h>
#endif
#endif /* _SYS_SIGNAL_H */
