#pragma once

#include <_cheader.h>
#include <sys/types.h>

_Begin_C_Header

#if (defined(__x86_64__))
# include <kernel/arch/x86_64/regs.h>
typedef struct {
	uint64_t mc_fpu_regs[64];
	struct regs mc_regs;
} mcontext_t;
#elif (defined(__aarch64__))
# include <kernel/arch/aarch64/regs.h>
typedef struct {
	uintptr_t mc_fpu_flags_13;
	uintptr_t mc_fpu_flags_12;
	uint64_t  mc_fpu_regs[64];
	uintptr_t mc_spsr;
	uintptr_t mc_elr;
	struct regs mc_regs;
} mcontext_t;
#else
# error "Need mcontext_t definition for arch."
#endif

typedef struct __ucontext_t ucontext_t;

struct __ucontext_t {
	sigset_t uc_sigmask;
	long uc__signo;
	long uc__interrupted_syscall;
	mcontext_t uc_mcontext;
	ucontext_t * uc_link;
};

_End_C_Header
