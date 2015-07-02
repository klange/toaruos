.set MB_MAGIC,              0x1BADB002
.set MB_FLAG_PAGE_ALIGN,    1 << 0
.set MB_FLAG_MEMORY_INFO,   1 << 1
.set MB_FLAG_GRAPHICS,      1 << 2
.set MB_FLAG_FLAT,          1 << 16
.set MB_FLAGS,              MB_FLAG_PAGE_ALIGN | MB_FLAG_MEMORY_INFO | MB_FLAG_GRAPHICS | MB_FLAG_FLAT
.set MB_CHECKSUM,           -(MB_MAGIC + MB_FLAGS)

.section .multiboot
.align 4

.extern bss_start
.extern end

/* Multiboot section */
.long MB_MAGIC
.long MB_FLAGS
.long MB_CHECKSUM
.long 0x00100000 /* header_addr */
.long 0x00100000 /* load_addr */
.long bss_start  /* load_end_addr */
.long end        /* bss_end_addr */
.long start      /* entry_addr */

/* Request linear graphics mode */
.long 0x00000000
.byte 0
.byte 0
.byte 32

/* .stack resides in .bss */
.section .stack, "aw", @nobits
stack_bottom:
.skip 16384 /* 16KiB */
stack_top:

.section .extra_files
.ifdef EMBEDDED_STARTFILES
.global embedded_packfile
.type embedded_packfile, @function
    .align 4096
embedded_packfile:
    .incbin "modpack.kop"

.global embedded_ramdisk_start
.type embedded_ramdisk_start, @function
.global embedded_ramdisk_end
.type embedded_ramdisk_end, @function
    .align 4096
embedded_ramdisk_start:
    .incbin "ramdisk.img"
embedded_ramdisk_end:
    .byte 0
.endif

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
