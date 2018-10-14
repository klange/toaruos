.code16
main:
	ljmp $0x0,$0x7c05
main2:
	mov $0x0, %ax
	mov %ax, %ds
	mov %ax, %ss
	mov $0x7b00, %ax
	mov %ax, %sp
	mov $0x500, %ax
	mov %ax, %es

	clc
	int $0x12
	mov %ax, (lower_mem)

	mov $0x0, %di
	call do_e820
	jc hang

	cli

	in $0x92, %al
	or $2, %al
	out %al, $0x92

	xor %eax, %eax
	mov %ds, %ax
	shl $4, %eax
	add $gdt_base, %eax
	mov %eax, gdtr+2
	mov $gdt_end, %eax
	sub $gdt_base, %eax
	mov %ax, gdtr
	lgdtw gdtr

	mov %cr0, %eax
	or $1, %eax
	mov %eax, %cr0

	mov $0x10, %ax
	mov %ax, %ds
	mov %ax, %es
	mov %ax, %fs
	mov %ax, %gs
	mov %ax, %ss

.global kmain
	ljmp $0x08,$kmain

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
	.word 0
	.long 0

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

.global mmap_ent
mmap_ent:
	.byte 0
	.byte 0

.global lower_mem
lower_mem:
	.byte 0
	.byte 0
