/**
 * @file kernel/arch/aarch64/entry.S
 * @brief Entrypoint stub.
 *
 * This gets us off of the bootstab's stack and onto
 * our own temporary stack, which we'll ideally switch
 * off of again when we start up the root process.
 */
.extern __bootstrap_stack_top
.globl start
start:
    ldr x30, =__bootstrap_stack_top
    mov sp, x30
    bl kmain
hang:
    b hang


