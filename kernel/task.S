.section .text
.align 4

/* Disable paging mask */
.set dp, 0x7FFFFFFF
/* Enable paging mask */
.set ep, 0x80000000

.global copy_page_physical
.type copy_page_physical, @function

copy_page_physical:
    /* Preserve contents of EBX */
    push %ebx

    /* Preserve contents of EFLAGS */
    pushf
    cli

    /* Load source and destination addresses */
    mov 12(%esp), %ebx
    mov 16(%esp), %ecx

    /* Get control register and disable paging*/
    mov %cr0, %edx
    and $dp, %edx
    mov %edx, %cr0

    /* Copy 4096 bytes */
    mov $0x400, %edx
.page_loop:
    /* Get word at source address */
    mov (%ebx), %eax

    /* Store it at destination address */
    mov %eax, (%ecx)

    /* Increment source and destination addresses to next word */
    add $4, %ebx
    add $4, %ecx

    /* One less word to copy */
    dec %edx
    jnz .page_loop

    /* Get control register again and enable paging */
    mov %cr0, %edx
    or  $ep, %edx
    mov %edx, %cr0

    /* Restore EFLAGS */
    popf

    /* Restore EBX */
    pop %ebx
    ret

/* Read EIP */
.global read_eip
.type read_eip, @function

read_eip:
    mov (%esp), %eax
    ret
