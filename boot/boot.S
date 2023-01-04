.code16
main:
	ljmp $0x0,$entry

entry:
	/* Set up initial segments */
	xor %ax, %ax
	mov %ax, %ds
	mov %ax, %ss

	/* Don't lose dl */
	mov %dl, boot_disk

	/* Initialize stack to just below us */
	mov $0x7c00, %ax
	mov %ax, %sp

	/* Prepare to switch to unreal mode */
	cli
	push %ds
	push %es

	/* Enable A20 */
	in $0x92, %al
	or $2, %al
	out %al, $0x92

	/* Switch to unreal mode */
	lgdtw gdtr
	mov %cr0, %eax
	or $1, %al
	mov %eax, %cr0
	jmp pmode
pmode:
	mov $0x10, %bx
	mov %bx, %ds
	mov %bx, %es
	and $0xfe, %al
	mov %eax, %cr0
	jmp unrealmode
unrealmode:
	pop %es
	pop %ds

	/* Clear the screen */
	mov  $0, %al
	movl $3840, %ecx
	movl $0xb8000, %edi
	addr32 rep stosb

	/* Check if we can actually go to long mode on this */
	mov $0x80000001, %eax
	cpuid
	and $0x20000000, %edx
	jnz can_long

	movl $str_Need_long, %esi
	call print_string
	jmp _oh_no

can_long:
	/* Spot check memory */
	movl $0x12345678, %eax
	movl $0x5000000, %ebx
	movl %eax, (%ebx)
	movl (%ebx), %edx
	cmp %edx, %eax
	jz good_memory

	movl $str_More_mem, %esi
	call print_string

_oh_no:
	jmp _oh_no

good_memory:
	/* Ask for drive params */
	mov $0x48, %ah
	mov boot_disk, %dl
	mov $drive_params, %si
	int $0x13

.extern _bss_start

boot_from_cd:
	/* Collect information on lower memory. */
	mov $0x500, %ax
	mov %ax, %es
	clc
	int $0x12
	mov %ax, lower_mem

	/* Collect information on upper memory. */
	mov $0x0, %di
	call do_e820
	jc hang

	/* Get video mode info */
	mov $0, %ax
	mov %ax, %es
	mov $vbe_cont_info, %di
	mov $0x4F00, %ax
	int $0x10

	/* Actually switch to protected mode. */
	mov %cr0, %eax
	or $1, %eax
	mov %eax, %cr0

	mov $0x10, %ax
	mov %ax, %ds
	mov %ax, %es
	mov %ax, %fs
	mov %ax, %gs
	mov %ax, %ss

	cli

.global bios_main
	ljmp $0x08,$bios_main

hang:
	jmp hang

do_e820:
	xor %ebx, %ebx
	xor %bp, %bp
	mov $0x534D4150, %edx
	mov $0xe820, %eax
	movl $0x1,%es:20(%di)
	mov $24, %ecx
	int $0x15
	jb  do_e820.failed
	mov $0x534D4150, %edx
	cmp %edx, %eax
	jne do_e820.failed
	test %ebx, %ebx
	je  do_e820.failed
	jmp do_e820.jmpin
do_e820.e820lp:
	mov $0xe820, %eax
	movl $0x1,%es:20(%di)
	mov $24, %ecx
	int $0x15
	jb do_e820.e820f
	mov $0x534D4150, %edx
do_e820.jmpin:
	jcxz do_e820.skipent
	cmp $20, %cl
	jbe do_e820.notext
	testb $0x1, %es:20(%di)
	je do_e820.skipent
do_e820.notext:
	mov %es:8(%di), %ecx
	or %es:12(%di), %ecx
	jz do_e820.skipent
	inc %bp
	add $24, %di
do_e820.skipent:
	test %ebx, %ebx
	jne do_e820.e820lp
do_e820.e820f:
	mov %bp, mmap_ent
	clc
	ret
do_e820.failed:
	stc
	ret

print_string:
	movl $0xb8000, %edi
print_string.loop:
	movb (%esi), %ah
	cmp $0, %ah
	je print_string.exit
	movb %ah, (%edi)
	inc %edi
	movb $7, (%edi)
	inc %esi
	inc %edi
	jmp print_string.loop
print_string.exit:
	ret

pm_stack:
	.quad 0

.global do_bios_call
do_bios_call:
.code32
	/* Standard function entry point stuff */
	push %ebp
	mov  %esp, %ebp
	push %eax
	push %ebx
	push %ecx
	push %edx
	push %esi
	push %edi
	/* Save stack because bios might mess it up? */
	movl %esp, %eax
	movl %eax, (pm_stack)

	/* Prepare intermediary mode */
	mov $0x20, %ax
	mov %ax, %ds
	mov %ax, %es
	mov %ax, %fs
	mov %ax, %gs
	mov %ax, %ss

	/* Enable intermediary mode */
	ljmp $0x18,$do_bios_call.0

do_bios_call.0:
.code16
	/* Disable protected mode */
	mov %cr0, %eax
	and $~1, %eax
	mov %eax, %cr0

	/* Jump to deactivate protected mode */
	ljmp $0x0,$do_bios_call.1

do_bios_call.1:
	/* Set up real mode segments */
	xor %ax, %ax
	mov %ax, %ds
	mov %ax, %es
	mov %ax, %fs
	mov %ax, %gs
	mov %ax, %ss

	/* Enable interrupts while BIOS is active */
	sti

	/* Function switch */
	movl 32(%esp), %ebx

	/* 1: Read disk */
	mov $0x01, %ax
	cmp %bx, %ax
	je do_bios_call.read_disk

	/* 2: Query mode index */
	mov $0x02, %ax
	cmp %bx, %ax
	je do_bios_call.query_mode

	/* 3: Set mode */
	mov $0x03, %ax
	cmp %bx, %ax
	je do_bios_call.set_mode

	mov $0x04, %ax
	cmp %bx, %ax
	je do_bios_call.test_key

	mov $0x05, %ax
	cmp %bx, %ax
	je do_bios_call.set_font

	/* Else: Bad call, jump to loop. */
	jmp do_bios_call.done

do_bios_call.read_disk:
	mov $0x42, %ah     /* Extended read */
	mov boot_disk, %dl /* Using our boot disk */
	mov $dap, %si      /* From the DAP below */
	int $0x13
	jmp do_bios_call.done

do_bios_call.query_mode:
	movl 36(%esp), %ecx
	mov $0x0, %ax
	mov %ax, %es
	mov $vbe_info, %edi
	mov $0x4F01, %ax
	int $0x10
	jmp do_bios_call.done

do_bios_call.set_mode:
	movl 36(%esp), %ebx
	mov $0x4F02, %ax
	int $0x10
	jmp do_bios_call.done

do_bios_call.test_key:
	movl 36(%esp), %ebx
	xor %ax, %ax
	mov %bl, %ah
	int $0x16
	movl %eax, 20(%esp)
	jmp do_bios_call.done

do_bios_call.set_font:
	movl 36(%esp), %ebp /* address of font data into ebp */
	mov $0x1100, %ax /* mode = load user-defined font */
	mov $17, %bh /* 17 bytes (rows) per character */
	mov $0, %bl  /* font block 0 */
	mov $0, %dx  /* starting from char 0 */
	mov $0x100, %cx /* write 256 glyphs */
	int $0x10
	jmp do_bios_call.done

do_bios_call.done:
	/* Disable interrupts again */
	cli

	/* Restore data segment, gdt */
	xor %ax,%ax
	mov %ax, %ds
	lgdtw gdtr

	/* Enable protected mode */
	mov %cr0, %eax
	or $1, %eax
	mov %eax, %cr0

	/* Jump to activate protected mode */
	ljmp $0x08,$do_bios_call.2

do_bios_call.2:
.code32
	/* Restore protected mode data segments */
	mov $0x10, %ax
	mov %ax, %ds
	mov %ax, %es
	mov %ax, %fs
	mov %ax, %gs
	mov %ax, %ss

	/* Restore stack */
	movl (pm_stack), %eax
	movl %eax, %esp

	/* Pop callee-saved registers we may messed with */
	pop %edi
	pop %esi
	pop %edx
	pop %ecx
	pop %ebx
	pop %eax
	pop %ebp

	/* Return */
	ret

.align 8
gdtr:
	.word gdt_end - gdt_base - 1
	.long gdt_base

gdt_base:
	.quad 0
	.word 0xFFFF
	.word 0
	.byte 0
	.byte 0x9a
	.byte 0xcf
	.byte 0

	.word 0xffff
	.word 0
	.byte 0
	.byte 0x92
	.byte 0xcf
	.byte 0

	.word 0xffff
	.word 0
	.byte 0
	.byte 0x9e
	.byte 0
	.byte 0

	.word 0xffff
	.word 0
	.byte 0
	.byte 0x92
	.byte 0
	.byte 0
gdt_end:

.global boot_disk
boot_disk:
	.byte 0

.global mmap_ent
mmap_ent:
	.byte 0
	.byte 0

.global lower_mem
lower_mem:
	.byte 0
	.byte 0

.align 4
.global dap
dap:
	.byte 16
	.byte 0 /* always 0 */
.global dap_sectors
dap_sectors:
	.word 1
.global dap_buffer
dap_buffer:
	.long 0x0
.global dap_lba_low
dap_lba_low:
	.long 0
.global dap_lba_high
dap_lba_high:
	.long 0

.align 4
drive_params:
	.word 0x1A
	.word 0 /* flags */
	.long 0 /* cylinders */
	.long 0 /* heads */
	.long 0 /* sectors */
	.quad 0 /* total sectors */
.global drive_params_bps
drive_params_bps:
	.word 0 /* bytes per sector */

.align 4
.global vbe_info
vbe_info:
	.word 0 /* attributes */
	.word 0 /* old shit (window a/b) */
	.word 0 /* Granulatory of banks, don't care. */
	.word 0 /* Window size, don't care. */
	.long 0 /* Segments... */
	.long 0 /* old bank switching thing */
.global vbe_info_pitch
vbe_info_pitch:
	.word 0 /* PITCH */
.global vbe_info_width
vbe_info_width:
	.word 0 /* WIDTH */
.global vbe_info_height
vbe_info_height:
	.word 0 /* HEIGHT */
	.word 0 /* w, y */
	.byte 0 /* planes */
.global vbe_info_bpp
vbe_info_bpp:
	.byte 0 /* bits per pixel */
	.byte 0 /* banks */
	.byte 0 /* Memory model */
	.byte 0 /* bank size */
	.byte 0 /* pages */
	.byte 0 /* reserved */
	.byte 0 /* RED mask */
	.byte 0 /* RED offset */
	.byte 0 /* GREEN mask */
	.byte 0 /* GREEN offset */
	.byte 0 /* BLUE maask */
	.byte 0 /* BLUE offset */
	.byte 0 /* ALPHA mask */
	.byte 0 /* ALPHA offset */
	.byte 0 /* Color attributes */
.global vbe_info_fbaddr
vbe_info_fbaddr:
	.long 0 /* Framebuffer address */
	.long 0 /* Extra memory offset */
	.word 0 /* Extra memory size */
	.zero 206 /* Other crap */

.align 4
vbe_cont_info:
	.ascii "VBE2"
	.word 0x200
	.long 0
	.long 0 /* caps */
.global vbe_cont_info_mode_off
vbe_cont_info_mode_off:
	.word 0 /* MODES */
vbe_cont_info_mode_seg:
	.word 0
	.zero 494

str_Need_long:
	.asciz "ToaruOS 2.0 requires a 64-bit processor."
str_More_mem:
	.asciz "ToaruOS 2.0 needs at least 128MiB of RAM, and 1GiB is recommended."

.global disk_space
disk_space:
	.zero 2048
