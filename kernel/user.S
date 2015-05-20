/* Return to Userspace (from thread creation) */

.global return_to_userspace
.type return_to_userspace, @function

return_to_userspace:
    pop %gs
    pop %fs
    pop %es
    pop %ds
    popa
    add $8, %esp
    iret

/* Enter userspace (ring3) */
.global enter_userspace
.type enter_userspace, @function

.set MAGIC, 0xDECADE21

enter_userspace:
    pushl %ebp
    mov %esp, %ebp
    mov 0xC(%ebp), %edx
    mov %edx, %esp
    pushl $MAGIC

    /* Segement selector */
    mov $0x23,%ax

    /* Save segement registers */
    mov %eax, %ds
    mov %eax, %es
    mov %eax, %fs
    mov %eax, %gs
    /* %ss is handled by iret */

    /* Store stack address in %eax */
    mov %esp, %eax

    /* Data segmenet with bottom 2 bits set for ring3 */
    pushl $0x23

    /* Push the stack address */
    pushl %eax

    /* Push flags and fix interrupt flag */
    pushf
    popl %eax

    /* Request ring3 */
    orl $0x200, %eax
    pushl %eax
    pushl $0x1B

    /* Push entry point */
    pushl 0x8(%ebp)

    iret
    popl %ebp
    ret
