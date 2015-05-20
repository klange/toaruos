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

/* Read EIP */
.global read_eip
.type read_eip, @function

read_eip:
    mov (%esp), %eax
    ret
