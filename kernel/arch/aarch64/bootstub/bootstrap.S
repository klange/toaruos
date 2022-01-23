.extern __bootstrap_stack_top
.globl start
start:
    ldr x30, =__bootstrap_stack_top
    mov sp, x30
    bl kmain
hang:
    b hang
