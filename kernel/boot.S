.set MB_MAGIC,              0x1BADB002
.set MB_FLAG_PAGE_ALIGN,    1 << 0
.set MB_FLAG_MEMORY_INFO,   1 << 1
.set MB_FLAG_GRAPHICS,      1 << 2
.set MB_FLAGS,              MB_FLAG_PAGE_ALIGN | MB_FLAG_MEMORY_INFO | MB_FLAG_GRAPHICS
.set MB_CHECKSUM,           -(MB_MAGIC + MB_FLAGS)

.section .multiboot
.align 4

/* Multiboot section */
.long MB_MAGIC
.long MB_FLAGS
.long MB_CHECKSUM
.long 0x00000000 /* header_addr */
.long 0x00000000 /* load_addr */
.long 0x00000000 /* load_end_addr */
.long 0x00000000 /* bss_end_addr */
.long 0x00000000 /* entry_addr */

/* Request linear graphics mode */
.long 0x00000000
.long 0
.long 0
.long 32

/* .stack resides in .bss */
.section .stack, "aw", @nobits
stack_bottom:
.skip 16384 /* 16KiB */
stack_top:

.section .text

.global start
.type start, @function

.extern kmain
.type kmain, @function

start:
    /* Setup our stack */
    mov $stack_top, %esp

    /* Make sure our stack is 16-byte aligned */
    and $-16, %esp

    pushl %esp
    pushl %eax /* Multiboot header magic */
    pushl %ebx /* Multiboot header pointer */

    /* Disable interrupts and call kernel proper */
    cli
    call kmain

    /* Clear interrupts and hang if we return from kmain */
    cli
hang:
    hlt
    jmp hang
