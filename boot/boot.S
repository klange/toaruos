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
	movl $str_Need_long_end - str_Need_long, %ecx
	movl $0xb8000, %edi
	addr32 rep movsb
	jmp _oh_no

can_long:
	/* Print "Loading..." */
	movl $str_Loading, %esi
	movl $str_Loading_end - str_Loading, %ecx
	movl $0xb8000, %edi
	addr32 rep movsb

	/* Use BIOS disk reads to locate the kernel and ramdisk
	 * and load them around the 2MB mark (by loading them
	 * in smaller chunks and copying those up thanks to
	 * our unreal mode %ds) */

	/* Ask for drive params */
	mov $0x48, %ah
	mov boot_disk, %dl
	mov $drive_params, %si
	int $0x13

	/* Are we a CD? Do we need to load more of ourselves? */
	mov drive_params_bps, %ax
	cmp $0x800, %ax
	je  boot_from_cd

	movl $str_Bad, %esi
	movl $str_Bad_end - str_Bad, %ecx
	movl $0xb8000, %edi
	addr32 rep movsb

_oh_no:
	jmp _oh_no

.extern _bss_start

	/* To load from the CD, we have 2048 byte sectors */
boot_from_cd:
	movl $0x4000000, %ebx
	movl $0x0, dap_lba_low       /* Sector 10h generally has our primary boot descriptor */
	movw $_bss_start, dap_buffer /* Load into root_data */

	/* Load one sector */
load_one:
	mov $0x42, %ah     /* Extended read */
	mov boot_disk, %dl /* Using our boot disk */
	mov $dap, %si      /* From the DAP below */
	int $0x13

	cmp $0, %ah
	jnz done

	/* Move the sector up */
	movl $_bss_start, %esi
	movl %ebx, %edi
	xor %ecx, %ecx
	movw drive_params_bps, %cx
	addr32 rep movsb

	/* Increment */
	movl dap_lba_low, %ecx
	add $1, %ecx
	movl %ecx, dap_lba_low
	xor %ecx, %ecx
	movw drive_params_bps, %cx
	add %ecx, %ebx

	/* Update spinner */
	movl ind_Spinny, %ecx
	inc %ecx
	and $0x3FF, %ecx
	movl %ecx, ind_Spinny
	shr $8, %ecx
	movb str_Spinny(%ecx), %ah
	mov $str_Loading_end - str_Loading - 2 + 0xb8000, %ecx
	movb %ah, (%ecx)

	jmp load_one

done:
	/* The entire CD is now in memory at 64MiB, hopefully. */

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
dap_lba_low:
	.long 0
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

str_Bad:
	.byte 'B',7,'a',7,'d',7,' ',7,'d',7,'i',7,'s',7,'k',7
str_Bad_end:

str_Loading:
	.byte 'L',7,'o',7,'a',7,'d',7,'i',7,'n',7,'g',7,'.',7,'.',7,'.',7,' ',7,' ',7
str_Loading_end:

str_Need_long:
	.byte 'T',7,'o',7,'a',7,'r',7,'u',7,'O',7,'S',7,' ',7,'2',7,' ',7,'r',7,'e',7,'q',7,'u',7,'i',7,'r',7,'e',7,'s',7,' ',7,'a',7,' ',7,'6',7,'4',7,'-',7,'b',7,'i',7,'t',7,' ',7,'p',7,'r',7,'o',7,'c',7,'e',7,'s',7,'s',7,'o',7,'r',7
str_Need_long_end:

str_Spinny:
	.byte '|','/','-','\\'
ind_Spinny:
	.long 0
