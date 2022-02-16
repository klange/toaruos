.extern __bootstrap_stack_top
.extern __bss_start
.extern __bss_size

.section ".bootstrap"
.globl start
start:
    ldr x30, =__bootstrap_stack_top
    mov x1, x4
    mov sp, x30
    ldr x5, =__bss_start
    ldr w6, =__bss_size
3:
    cbz w6, 4f
    str xzr, [x5], #8
    sub w6, w6, #1
    cbnz w6, 3b
4:
    bl kmain
hang:
    b hang

.section ".rodata"
.align 12
.globl _kernel_start
_kernel_start:
.incbin "misaka-kernel"
.globl _kernel_end
_kernel_end:

.align 12
.globl _ramdisk_start
_ramdisk_start:
.incbin "ramdisk.igz"
.global _ramdisk_end
_ramdisk_end:
