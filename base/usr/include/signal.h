#pragma once

#include <_cheader.h>
#include <sys/signal.h>
#include <sys/signal_defs.h>

_Begin_C_Header

typedef _sig_func_ptr sighandler_t;

#define SIG_DFL ((_sig_func_ptr)0)/* Default action */
#define SIG_IGN ((_sig_func_ptr)1)/* Ignore action */
#define SIG_ERR ((_sig_func_ptr)-1)/* Error return */

typedef int sig_atomic_t;

extern sighandler_t signal(int signum, sighandler_t handler);
extern int raise(int sig);
extern int sigaction(int signum, struct sigaction *act, struct sigaction *oldact);
extern int sigemptyset(sigset_t *);
extern int sigfillset(sigset_t * set);
extern int sigaddset(sigset_t * set, int signum);
extern int sigdelset(sigset_t * set, int signum);
extern int sigismember(sigset_t * set, int signum);
extern int sigprocmask(int how, const sigset_t * set, sigset_t * oset);
extern int sigpending(sigset_t * set);
extern int sigsuspend(const sigset_t * set);
extern int sigwait(const sigset_t * set, int * sig);

_End_C_Header
