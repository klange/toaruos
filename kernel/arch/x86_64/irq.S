/**
 * @file kernel/arch/x86_64/irq.S
 * @brief x86-64 interrupt entry points
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
 */
.section .text
.align 8

.macro IRQ index byte
    .global _irq\index
    .type _irq\index, @function
    _irq\index:
        pushq $0x00
        pushq $\byte
        jmp isr_common
.endm

.macro ISR_NOERR index
    .global _isr\index
    .type _isr\index, @function
    _isr\index:
        pushq $0x00
        pushq $\index
        jmp isr_common
.endm

.macro ISR_ERR index
    .global _isr\index
    .type _isr\index, @function
    _isr\index:
        pushq $\index
        jmp isr_common
.endm

/* Interrupt Requests */
ISR_NOERR 0
ISR_NOERR 1
//ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR   29
ISR_ERR   30
ISR_NOERR 31
IRQ 0, 32
IRQ 1, 33
IRQ 2, 34
IRQ 3, 35
IRQ 4, 36
IRQ 5, 37
IRQ 6, 38
IRQ 7, 39
IRQ 8, 40
IRQ 9, 41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47

/* syscall entry point */
ISR_NOERR 127

.global _isr123
.type _isr123, @function
_isr123:
    /* Acknowledge IPI */
    pushq %r12
    mov (lapic_final)(%rip), %r12
    add $0xb0, %r12
    movl $0, (%r12)
    popq %r12
    /* Then we can proceed! */
    pushq $0x00
    pushq $123
    jmp isr_common


.global _isr124
.type _isr124, @function
_isr124:
    pushq %r12
    mov %cr3, %r12
    mov %r12, %cr3
    mov (lapic_final)(%rip), %r12
    add $0xb0, %r12
    movl $0, (%r12)
    popq %r12
    iretq

/* No op, used to signal sleeping processor to wake and check the queue. */
.extern lapic_final
.global _isr126
.type _isr126, @function
_isr126:
    pushq %r12
    mov (lapic_final)(%rip), %r12
    add $0xb0, %r12
    movl $0, (%r12)
    popq %r12
    iretq

/* Fatal signal, stop everything. */
.global _isr125
.type _isr125, @function
_isr125:
    cli
1:
    hlt
    jmp 1b

/* Fatal signal, stop everything. */
.global _isr2
.type _isr2, @function
_isr2:
    cli
1:
    hlt
    jmp 1b

.macro _swapgs
    cmpq $8, 24(%rsp)
    je 1f
    swapgs
1:
.endm

.extern isr_handler
.type isr_handler, @function

.global isr_common
isr_common:
    /* Save all registers */
    _swapgs
    push %rax
    push %rbx
    push %rcx
    push %rdx
    push %rsi
    push %rdi
    push %rbp
    push %r8
    push %r9
    push %r10
    push %r11
    push %r12
    push %r13
    push %r14
    push %r15

    cld

    /* Call interrupt handler */
    mov %rsp, %rdi
    call isr_handler

    /* Restore all registers */
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rbp
    pop %rdi
    pop %rsi
    pop %rdx
    pop %rcx
    pop %rbx
    pop %rax

    _swapgs

    /* Cleanup error code and interrupt # */
    add $16, %rsp

    /* Return from interrupt  */
    iretq


.global arch_save_context
.type arch_save_context, @function
arch_save_context:
    leaq 8(%rsp), %rax
    movq %rax, 0(%rdi)
    movq %rbp, 8(%rdi)
    movq (%rsp), %rax
    movq %rax, 16(%rdi)
    movq $0xc0000100, %rcx
    rdmsr
    movl %eax, 24(%rdi)
    movl %edx, 28(%rdi)
    movq %rbx, 32(%rdi)
    movq %r12, 40(%rdi)
    movq %r13, 48(%rdi)
    movq %r14, 56(%rdi)
    movq %r15, 64(%rdi)
    xor %rax, %rax
    retq

.global arch_restore_context
.type arch_restore_context, @function
arch_restore_context:
    mov  %gs:0x10,%rax
    cmp  %gs:0x0,%rax
    je   1f
    lock andl $0xFFFFfff7,0x14(%rax)
1:
    movq 0(%rdi), %rsp
    movq 8(%rdi), %rbp
    movl 24(%rdi), %eax
    movl 28(%rdi), %edx
    movq $0xc0000100, %rcx
    wrmsr
    movq 32(%rdi), %rbx
    movq 40(%rdi), %r12
    movq 48(%rdi), %r13
    movq 56(%rdi), %r14
    movq 64(%rdi), %r15
    movq $1, %rax
    jmpq *16(%rdi)

.global arch_enter_tasklet
.type arch_enter_tasklet, @function
arch_enter_tasklet:
    popq %rdi
    popq %rsi
    jmpq *%rsi


.extern syscall_centry
.global syscall_entry
.type syscall_entry, @function
syscall_entry:
    swapgs             /* SYSCALL only happens from userspace, so we must always swap gs */
    mov %rsp, %gs:0x78 /* Store user RSP temporarily */
    mov %gs:0x70, %rsp /* Restore kernel stack for this thread */

    /* Normal `struct regs` layout, same as what we'd get on an interrupt */
    pushq $0x23        /* SS */
    pushq %gs:0x78     /* RSP */
    push %r11          /* RFLAGS - SYSCALL stores in r11 */
    pushq $0x2b        /* CS */
    push %rcx          /* RIP - SYSCALL stores in rcx */

    pushq $0           /* Dummy error code */
    pushq $0           /* Dummy interrupt number */

    push %rax
    push %rbx
    pushq $0           /* rcx is not valid, set to zero */
    push %rdx
    push %rsi
    push %rdi
    push %rbp
    push %r8
    push %r9
    push %r10
    pushq $0           /* r11 is not valid, set to zero */
    push %r12
    push %r13
    push %r14
    push %r15

    mov %rsp, %rdi
    call syscall_centry

    pop %r15
    pop %r14
    pop %r13
    pop %r12
    add $8, %rsp
    pop %r10
    pop %r9
    pop %r8
    pop %rbp
    pop %rdi
    pop %rsi
    pop %rdx
    add $8, %rsp
    pop %rbx
    pop %rax

    add $16, %rsp

    pop %rcx
    add $8, %rsp
    pop %r11
    pop %rsp

    swapgs
    sysretq
