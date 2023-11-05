#pragma once

#include <_cheader.h>
#include <stdint.h>

_Begin_C_Header

#if defined(__x86_64__)
# include <kernel/arch/x86_64/regs.h>
# define uregs_syscall_result(r) ((r)->rax)
# define uregs_syscall_num(r)    ((r)->rax)
# define uregs_syscall_arg1(r)   ((r)->rdi)
# define uregs_syscall_arg2(r)   ((r)->rsi)
# define uregs_syscall_arg3(r)   ((r)->rdx)
# define uregs_syscall_arg4(r)   ((r)->r10)
# define uregs_syscall_arg5(r)   ((r)->r8)
# define uregs_ip(r)             ((r)->rip)
# define uregs_bp(r)             ((r)->rbp)
# define UREGS_FMT \
		"  $rip=0x%016lx\n" \
		"  $rsi=0x%016lx,$rdi=0x%016lx,$rbp=0x%016lx,$rsp=0x%016lx\n" \
		"  $rax=0x%016lx,$rbx=0x%016lx,$rcx=0x%016lx,$rdx=0x%016lx\n" \
		"  $r8= 0x%016lx,$r9= 0x%016lx,$r10=0x%016lx,$r11=0x%016lx\n" \
		"  $r12=0x%016lx,$r13=0x%016lx,$r14=0x%016lx,$r15=0x%016lx\n" \
		"  cs=0x%016lx  ss=0x%016lx rflags=0x%016lx int=0x%02lx err=0x%02lx\n"
# define UREGS_ARGS(r) \
		(r)->rip, \
		(r)->rsi, (r)->rdi, (r)->rbp, (r)->rsp, \
		(r)->rax, (r)->rbx, (r)->rcx, (r)->rdx, \
		(r)->r8,  (r)->r9,  (r)->r10, (r)->r11, \
		(r)->r12, (r)->r13, (r)->r14, (r)->r15, \
		(r)->cs,  (r)->ss,  (r)->rflags, (r)->int_no, (r)->err_code
#elif defined(__aarch64__)
# include <kernel/arch/aarch64/regs.h>
# define uregs_syscall_result(r) ((r)->x0)
# define uregs_syscall_num(r)    ((r)->x0)
# define uregs_syscall_arg1(r)   ((r)->x1)
# define uregs_syscall_arg2(r)   ((r)->x2)
# define uregs_syscall_arg3(r)   ((r)->x3)
# define uregs_syscall_arg4(r)   ((r)->x4)
# define uregs_syscall_arg5(r)   ((r)->x5)
# define uregs_ip(r)             ((r)->elr)
# define uregs_bp(r)             ((r)->x29)
# define UREGS_FMT \
		" $x00=0x%016lx,$x01=0x%016lx,$x02=0x%016lx,$x03=0x%016lx\n" \
		" $x04=0x%016lx,$x05=0x%016lx,$x06=0x%016lx,$x07=0x%016lx\n" \
		" $x08=0x%016lx,$x09=0x%016lx,$x10=0x%016lx,$x11=0x%016lx\n" \
		" $x12=0x%016lx,$x13=0x%016lx,$x14=0x%016lx,$x15=0x%016lx\n" \
		" $x16=0x%016lx,$x17=0x%016lx,$x18=0x%016lx,$x19=0x%016lx\n" \
		" $x20=0x%016lx,$x21=0x%016lx,$x22=0x%016lx,$x23=0x%016lx\n" \
		" $x24=0x%016lx,$x25=0x%016lx,$x26=0x%016lx,$x27=0x%016lx\n" \
		" $x28=0x%016lx,$x29=0x%016lx,$x30=0x%016lx\n" \
		" sp=0x%016lx    elr=0x%016lx\n"
# define UREGS_ARGS(r) \
		(r)->x0, (r)->x1, (r)->x2, (r)->x3, (r)->x4, (r)->x5, (r)->x6, (r)->x7, \
		(r)->x8, (r)->x9, (r)->x10, (r)->x11, (r)->x12, (r)->x13, (r)->x14, (r)->x15, \
		(r)->x16, (r)->x17, (r)->x18, (r)->x19, (r)->x20, (r)->x21, (r)->x22, (r)->x23, \
		(r)->x24, (r)->x25, (r)->x26, (r)->x27, (r)->x28, (r)->x29, (r)->x30, \
		(r)->user_sp, (r)->elr
#else
# error Unsupported architecture
#endif

struct URegs {
	struct regs;
#if defined(__aarch64__)
	uint64_t elr;
#endif
};


_End_C_Header
