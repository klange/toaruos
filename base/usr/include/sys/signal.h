#pragma once

#include <_cheader.h>
#include <sys/types.h>

_Begin_C_Header
#define SIGEV_NONE   1
#define SIGEV_SIGNAL 2
#define SIGEV_THREAD 3

/* These should not overlap with the signal-specific ones */
#define SI_USER    0
#define SI_QUEUE   -1
#define SI_TIMER   -2
#define SI_ASYNCIO -3
#define SI_MESGQ   -4

/* si_code values for SIGILL */
#define ILL_ILLOPC 1
#define ILL_ILLOPN 2
#define ILL_ILLADR 3
#define ILL_ILLTRP 4
#define ILL_PRVOPC 5
#define ILL_PRVREG 6
#define ILL_COPROC 7
#define ILL_BADSTK 8

/* si_code values for SIGILL */
#define FPE_INTDIV 1
#define FPE_INTOVF 2
#define FPE_FLTDIV 3
#define FPE_FLTOVF 4
#define FPE_FLTUND 5
#define FPE_FLTRES 6
#define FPE_FLTINV 7
#define FPE_FLTSUB 8

/* si_code values for SIGVSEGV */
#define SEGV_MAPERR 1
#define SEGV_ACCERR 2

/* si_code values for SIGBUS */
#define BUS_ADRALN 1
#define BUS_ADRERR 2
#define BUS_OBJERR 3

/* si_code values for SIGTRAP */
#define TRAP_BRKPT 1
#define TRAP_TRACE 2

/* si_code values for SIGCHLD */
#define CLD_EXITED 1
#define CLD_KILLED 2
#define CLD_DUMPED 3
#define CLD_TRAPPED 4
#define CLD_STOPPED 5
#define CLD_CONTINUED 6

#define SA_NOCLDSTOP 1
#define SA_SIGINFO   2
#define SA_NODEFER   4
#define SA_RESETHAND 8
#define SA_RESTART   16

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
	void *       si_addr;
	pid_t        si_pid;
	uid_t        si_uid;
	int          si_errno;
	int          si_status;
} siginfo_t;

typedef unsigned long sigset_t;
typedef void (*_sig_func_ptr)(int);

struct sigaction {
	int         sa_flags;
	sigset_t    sa_mask;
	union {
		_sig_func_ptr _handler;
		void      (*_sigaction)( int, siginfo_t *, void * );
	} _signal_handlers;
};

#include <bits/ucontext.h>


extern int kill(pid_t, int);
_End_C_Header
