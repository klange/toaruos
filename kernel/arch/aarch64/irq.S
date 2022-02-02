/**
 * @file kernel/arch/aarch64/irq.S
 * @brief Exception entry points.
 *
 * These are still very hacky and rudimentary and we're
 * probably saving way more context than we actually need.
 */
sync_common:
    /* Push all regular registers */
    stp x0, x1, [sp, #-16]!
    stp x2, x3, [sp, #-16]!
    stp x4, x5, [sp, #-16]!
    stp x6, x7, [sp, #-16]!
    stp x8, x9, [sp, #-16]!
    stp x10, x11, [sp, #-16]!
    stp x12, x13, [sp, #-16]!
    stp x14, x15, [sp, #-16]!
    stp x16, x17, [sp, #-16]!
    stp x18, x19, [sp, #-16]!
    stp x20, x21, [sp, #-16]!
    stp x22, x23, [sp, #-16]!
    stp x24, x25, [sp, #-16]!
    stp x26, x27, [sp, #-16]!
    stp x28, x29, [sp, #-16]!
    /* combine x30 (link register) with stack */
    mrs x0, SP_EL0
    stp x30, x0, [sp, #-16]!
    /* Pass the current stack as the first arg (struct regs *r) */
    mov x0, sp
    /* Make sure x18 has our kernel CPU-local pointer */
    mrs x18, TPIDR_EL1
    .extern aarch64_sync_enter
    bl aarch64_sync_enter

    mrs x0, SPSR_EL1
    and w0, w0, #0x200000
    cbz w0, _sync_cont
    mrs x0, MDSCR_EL1
    orr x0, x0, #1
    msr MDSCR_EL1, x0
    _sync_cont:

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

fault_common:
    stp x0, x1, [sp, #-16]!
    stp x2, x3, [sp, #-16]!
    stp x4, x5, [sp, #-16]!
    stp x6, x7, [sp, #-16]!
    stp x8, x9, [sp, #-16]!
    stp x10, x11, [sp, #-16]!
    stp x12, x13, [sp, #-16]!
    stp x14, x15, [sp, #-16]!
    stp x16, x17, [sp, #-16]!
    stp x18, x19, [sp, #-16]!
    stp x20, x21, [sp, #-16]!
    stp x22, x23, [sp, #-16]!
    stp x24, x25, [sp, #-16]!
    stp x26, x27, [sp, #-16]!
    stp x28, x29, [sp, #-16]!
    mrs x0, SP_EL0
    stp x30, x0, [sp, #-16]!
    mov x0, sp
    mrs x18, TPIDR_EL1
    .extern aarch64_fault_enter
    bl aarch64_fault_enter
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

irq_common:
    stp x0, x1, [sp, #-16]!
    stp x2, x3, [sp, #-16]!
    stp x4, x5, [sp, #-16]!
    stp x6, x7, [sp, #-16]!
    stp x8, x9, [sp, #-16]!
    stp x10, x11, [sp, #-16]!
    stp x12, x13, [sp, #-16]!
    stp x14, x15, [sp, #-16]!
    stp x16, x17, [sp, #-16]!
    stp x18, x19, [sp, #-16]!
    stp x20, x21, [sp, #-16]!
    stp x22, x23, [sp, #-16]!
    stp x24, x25, [sp, #-16]!
    stp x26, x27, [sp, #-16]!
    stp x28, x29, [sp, #-16]!
    mrs x0, SP_EL0
    stp x30, x0, [sp, #-16]!
    mov x0, sp
    mrs x18, TPIDR_EL1
    .extern aarch64_irq_enter
    bl aarch64_irq_enter
    mrs x0, SPSR_EL1
    and w0, w0, #0x200000
    cbz w0, _irq_cont
    mrs x0, MDSCR_EL1
    orr x0, x0, #1
    msr MDSCR_EL1, x0
    _irq_cont:
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

.globl _exception_vector
.balign 0x800
_exception_vector:

/* AArch64 exception vectors are divided up;
 * our first set of vectors is for if we are using SP0
 * in EL1, which we don't currently do so these should
 * all probably just be loop branches to catch this. */
_exc_sp0_sync:
    b .
.balign 0x80
_exc_sp0_irq:
    b .
.balign 0x80
_exc_sp0_fiq:
    b .
.balign 0x80
_exc_sp0_serror:
    b .

/* These are EL1-EL1 fault handlers, for when we encounter
 * an exception in the kernel. We normally have interrupts
 * masked in the kernel so we should only see synchronous
 * exceptions - faults. */
.balign 0x80
_exc_spx_sync:
    b fault_common
.balign 0x80
_exc_spx_irq:
    b .
.balign 0x80
_exc_spx_fiq:
    b .
.balign 0x80
_exc_spx_serror:
    b .

/* These are EL0-EL1 transition handlers. Synchronous is going
 * to be faults and system calls (SVC requests); */
.balign 0x80
_exc_lower_sync:
    b sync_common

/* Actual interrupts */
.balign 0x80
_exc_lower_irq:
    b irq_common

/* "Fast" interrupts. TODO */
.balign 0x80
_exc_lower_fiq:
    b .

.balign 0x80
_exc_lower_serror:
    b .
.balign 0x80

/* These are going to be calls from 32-bit userspace, which we're
 * not going to support, so just blank all of these out as well. */
_exc_lower_32_sync:
    b .
.balign 0x80
_exc_lower_32_irq:
    b .
.balign 0x80
_exc_lower_32_fiq:
    b .
.balign 0x80
_exc_lower_32_serror:
    b .
