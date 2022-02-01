.extern __bootstrap_stack_top
.globl start
start:
    ldr x30, =__bootstrap_stack_top
    mov sp, x30
    bl kmain
hang:
    b hang

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
    add x1, x1, 20
_restore_context_loop:
    ldxr w2, [x1]
    and  w2, w2, 0xFFFFfff7
    stlxr w4, w2, [x1]
    cbnz w4, _restore_context_loop
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


sync_common:
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
    .extern aarch64_sync_enter
    bl aarch64_sync_enter
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
.balign 0x80
_exc_lower_sync:
    b sync_common
.balign 0x80
_exc_lower_irq:
    b irq_common
.balign 0x80
_exc_lower_fiq:
    b .
.balign 0x80
_exc_lower_serror:
    b .
.balign 0x80
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
