#pragma once

#include <_cheader.h>
#include <sys/types.h>

_Begin_C_Header
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

#if 0
#define SIG_SETMASK 0
#define SIG_BLOCK 1
#define SIG_UNBLOCK 2
#endif

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


extern int kill(pid_t, int);
_End_C_Header
