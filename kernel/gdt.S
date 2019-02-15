.section .text
.align 4

.global gdt_flush
.type gdt_flush, @function

gdt_flush:
    /* Load GDT */
    push %ebp
    mov  %esp, %ebp
    mov 8(%ebp), %eax
    lgdt (%eax)

    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %ss
    mov %ax, %gs

    ljmp $0x08, $.flush
.flush:
    mov %ebp, %esp
    pop %ebp
    ret
