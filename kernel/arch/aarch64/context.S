/**
 * @file kernel/arch/aarch64/context.S
 * @brief Kernel context switching utilities.
 *
 * arch_save_context and arch_restore_context are basically
 * setjmp and longjmp. _restore_context will also set the
 * previous thread as no longer running.
 */
.globl arch_save_context
arch_save_context:
    /* x0 has our struct pointer */
    mrs x1, TPIDR_EL0
    mov x2, sp
    stp x2, x29,  [x0]
    stp x30, x1,  [x0, (1 * 16)]
    stp x19, x20, [x0, (2 * 16)]
    stp x21, x22, [x0, (3 * 16)]
    stp x23, x24, [x0, (4 * 16)]
    stp x25, x26, [x0, (5 * 16)]
    stp x27, x28, [x0, (6 * 16)]
    mrs x1, ELR_EL1
    mrs x2, SPSR_EL1
    stp x1, x2,   [x0, (7 * 16)]
    mov x0, 0
    ret

.globl arch_restore_context
arch_restore_context:
    ldr x1, [x18, 16] /* get previous */
    ldr x2, [x18, 0]  /* get current */
    cmp x2, x1 /* compare current to prev, into x2 */
    beq _restore_context_same
    /* 20 is the offset of ->status. */
    add x1, x1, 20
    /* equivalent to __atomic_and_and_fetch */
_restore_context_loop:
    ldxr w2, [x1] /* Load exclusive */
    and  w2, w2, 0xFFFFfff7 /* Unset running */
    stlxr w4, w2, [x1] /* Store exclusive */
    cbnz w4, _restore_context_loop /* try again if we failed */
_restore_context_same:
    /* x0 has our struct pointer */
    ldp x2, x29,  [x0]
    ldp x30, x1,  [x0, (1 * 16)]
    ldp x19, x20, [x0, (2 * 16)]
    ldp x21, x22, [x0, (3 * 16)]
    ldp x23, x24, [x0, (4 * 16)]
    ldp x25, x26, [x0, (5 * 16)]
    ldp x27, x28, [x0, (6 * 16)]
    msr TPIDR_EL0, x1
    mov sp, x2
    ldp x1, x2,   [x0, (7 * 16)]
    msr ELR_EL1, x1
    msr SPSR_EL1, x2
    mov x0, 1
    ret

/**
 * @brief Start of userspace thread.
 */
.globl arch_resume_user
arch_resume_user:
    ldp x30, x0, [sp], #16
    msr SP_EL0, x0
    ldp x28, x29, [sp], #16
    ldp x26, x27, [sp], #16
    ldp x24, x25, [sp], #16
    ldp x22, x23, [sp], #16
    ldp x20, x21, [sp], #16
    ldp x18, x19, [sp], #16
    ldp x16, x17, [sp], #16
    ldp x14, x15, [sp], #16
    ldp x12, x13, [sp], #16
    ldp x10, x11, [sp], #16
    ldp x8, x9, [sp], #16
    ldp x6, x7, [sp], #16
    ldp x4, x5, [sp], #16
    ldp x2, x3, [sp], #16
    ldp x0, x1, [sp], #16
    eret

