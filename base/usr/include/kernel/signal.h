#pragma once

#include <stdint.h>
#include <sys/types.h>
#include <sys/signal.h>

#if defined(__x86_64__)
#include <kernel/arch/x86_64/regs.h>
#elif defined(__aarch64__)
#include <kernel/arch/aarch64/regs.h>
#else
#error "no regs"
#endif

typedef struct {
	int signum;
} signal_t;

extern void fix_signal_stacks(void);
extern int send_signal(pid_t process, int signal, int force_root);
extern int send_signal_info(pid_t process, int signal, int force_root, siginfo_t *cause);
extern void return_from_signal_handler(struct regs*);
extern void process_check_signals(struct regs*);
extern int signal_await(sigset_t awaited, int * sig);

/* These have actually moved to 'process', but they're signal-related
 * so they'll stay in this header for now. */
extern int group_send_signal(pid_t group, int signal, int force_root);
extern int session_send_signal(pid_t session, int signal, int force_root);
