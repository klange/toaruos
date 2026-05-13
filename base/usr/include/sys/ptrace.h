#pragma once

#include <_cheader.h>
#include <sys/types.h>

_Begin_C_Header

enum __ptrace_request {
	PTRACE_ATTACH,
	PTRACE_CONT,
	PTRACE_DETACH,
	PTRACE_TRACEME,
	PTRACE_GETREGS,
	PTRACE_PEEKDATA,
	PTRACE_SIGNALS_ONLY_PLZ,
	PTRACE_POKEDATA,
	PTRACE_SINGLESTEP,
	PTRACE_SETREGS,
	PTRACE_SETOPTIONS,
};

enum __ptrace_event {
	PTRACE_EVENT_SYSCALL_ENTER,
	PTRACE_EVENT_SYSCALL_EXIT,
	PTRACE_EVENT_SINGLESTEP,
};

/* Trying to match the Linux values for these option bits */
#define PTRACE_O_TRACEFORK  (1 << 1)
#define PTRACE_O_TRACECLONE (1 << 3)
#define PTRACE_O_TRACEEXEC  (1 << 4)

#ifndef __kernel__
extern long ptrace(enum __ptrace_request request, pid_t pid, void * addr, void * data);
#endif

_End_C_Header
