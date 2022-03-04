#pragma once
#include <kernel/types.h>
#include <kernel/process.h>

#define FD_INRANGE(FD) \
	((FD) < (int)this_core->current_process->fds->length && (FD) >= 0)
#define FD_ENTRY(FD) \
	(this_core->current_process->fds->entries[(FD)])
#define FD_CHECK(FD) \
	(FD_INRANGE(FD) && FD_ENTRY(FD))
#define FD_OFFSET(FD) \
	(this_core->current_process->fds->offsets[(FD)])
#define FD_MODE(FD) \
	(this_core->current_process->fds->modes[(FD)])

#define PTR_INRANGE(PTR) \
	((uintptr_t)(PTR) > this_core->current_process->image.entry && ((uintptr_t)(PTR) < 0x8000000000000000))
#define PTR_VALIDATE(PTR) \
	do { if (ptr_validate((void *)(PTR), __func__)) return -EINVAL; } while (0)
extern int ptr_validate(void * ptr, const char * syscall);

extern long arch_syscall_number(struct regs * r);
extern long arch_syscall_arg0(struct regs * r);
extern long arch_syscall_arg1(struct regs * r);
extern long arch_syscall_arg2(struct regs * r);
extern long arch_syscall_arg3(struct regs * r);
extern long arch_syscall_arg4(struct regs * r);

extern long arch_stack_pointer(struct regs * r);
extern long arch_user_ip(struct regs * r);

extern void arch_syscall_return(struct regs * r, long retval);

extern void syscall_handler(struct regs * r);
