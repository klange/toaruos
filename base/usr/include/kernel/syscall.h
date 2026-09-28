#pragma once
#include <kernel/types.h>
#include <kernel/process.h>

#define FD_PTR_CLOEXEC 1
#define FD_PTR_CLOFORK 2

#define FD_PTR_MASK(ptr) ((struct fs_file_description*)(ptr & ~3))

#define FD_INRANGE(FD) \
	((FD) < (int)this_core->current_process->fds->length && (FD) >= 0)

#define FD_FILE(FD)   FD_PTR_MASK(this_core->current_process->fds->entries[(FD)])
#define FD_ENTRY(FD)  FD_FILE(FD)->inode
#define FD_CHECK(FD)  (FD_INRANGE(FD) && FD_FILE(FD) && FD_ENTRY(FD))
#define FD_OFFSET(FD) FD_FILE(FD)->offset
#define FD_MODE(FD)   FD_FILE(FD)->flags

#define FD_CLO_MODE(FD) (this_core->current_process->fds->entries[(FD)] & 3)

static inline void fd_set_mode_flags(int fd, uintptr_t modes) {
	uintptr_t ptr = (uintptr_t)FD_FILE(fd);
	this_core->current_process->fds->entries[fd] = ptr | modes;
}

#define PTR_INRANGE(PTR) \
	((uintptr_t)(PTR) < 0x8000000000000000)
#define PTR_VALIDATE(PTR) \
	do { if (ptr_validate((void *)(PTR), __func__)) return -EFAULT; } while (0)
extern int ptr_validate(void * ptr, const char * syscall);

extern long arch_syscall_number(struct regs * r);
extern long arch_syscall_arg0(struct regs * r);
extern long arch_syscall_arg1(struct regs * r);
extern long arch_syscall_arg2(struct regs * r);
extern long arch_syscall_arg3(struct regs * r);
extern long arch_syscall_arg4(struct regs * r);
extern long arch_syscall_arg5(struct regs * r);

extern long arch_stack_pointer(struct regs * r);
extern long arch_user_ip(struct regs * r);

extern void arch_syscall_return(struct regs * r, long retval);

extern void syscall_handler(struct regs * r);
