#pragma once

#include <sys/signal.h>
#include <sys/signal_defs.h>

typedef _sig_func_ptr sighandler_t;

#define SIG_DFL ((_sig_func_ptr)0)/* Default action */
#define SIG_IGN ((_sig_func_ptr)1)/* Ignore action */
#define SIG_ERR ((_sig_func_ptr)-1)/* Error return */

typedef int sig_atomic_t;

extern sighandler_t signal(int signum, sighandler_t handler);
