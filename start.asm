[BITS 32]
ALIGN 4

; ToAruOS
; Copyright 2011 ToAruOS Kernel Development Group
; See main.c for licensing terms (NCSA)


; Kernel Multiboot Headers
mboot:
	MULTIBOOT_PAGE_ALIGN	equ 1<<0
	MULTIBOOT_MEMORY_INFO	equ 1<<1
	MULTIBOOT_HEADER_MAGIC	equ 0x1BADB002
	MULTIBOOT_HEADER_FLAGS	equ MULTIBOOT_PAGE_ALIGN | MULTIBOOT_MEMORY_INFO
	MULTIBOOT_CHECKSUM		equ -(MULTIBOOT_HEADER_MAGIC + MULTIBOOT_HEADER_FLAGS)
	EXTERN code, bss, end
	; GRUB Multiboot header, boot signature
	dd MULTIBOOT_HEADER_MAGIC
	dd MULTIBOOT_HEADER_FLAGS
	dd MULTIBOOT_CHECKSUM

; Main entrypoint
global start
start:
	mov esp, _sys_stack
	; Push the incoming mulitboot headers
	push ebx
	; Disable interrupts
	cli
	; Call the C entry
	extern	main
	call	main
	jmp		$


; Global Descriptor Table
global gdt_flush
extern gp
gdt_flush:
	lgdt [gp]
	mov ax, 0x10
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax
	mov ss, ax
	jmp 0x08:flush2
flush2:
	ret

; Interrupt Descriptor Table
global idt_load
extern idtp
idt_load:
	lidt [idtp]
	ret

; Interrupt Service Routines
%macro ISR_NOERR 1
	global _isr%1
	_isr%1:
		cli
		push byte 0
		push byte %1
		jmp isr_common_stub
%endmacro

%macro ISR_ERR 1
	global _isr%1
	_isr%1:
		cli
		push byte %1
		jmp isr_common_stub
%endmacro

; Standard X86 interrupt service routines
ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_NOERR 17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31

extern fault_handler

isr_common_stub:
	pusha
	push ds
	push es
	push fs
	push gs
	mov ax, 0x10
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax
	mov eax, esp
	push eax
	; Call the C kernel fault handler
	mov eax, fault_handler
	call eax
	pop eax
	pop gs
	pop fs
	pop es
	pop ds
	popa
	add esp, 8
	iret

%macro IRQ_ENTRY 2
	global _irq%1
	_irq%1:
		cli
		push byte 0
		push byte %2
		jmp irq_common_stub
%endmacro

; Interrupt Requests
IRQ_ENTRY 0, 32
IRQ_ENTRY 1, 33
IRQ_ENTRY 2, 34
IRQ_ENTRY 3, 35
IRQ_ENTRY 4, 36
IRQ_ENTRY 5, 37
IRQ_ENTRY 6, 38
IRQ_ENTRY 7, 39
IRQ_ENTRY 8, 40
IRQ_ENTRY 9, 41
IRQ_ENTRY 10, 42
IRQ_ENTRY 11, 43
IRQ_ENTRY 12, 44
IRQ_ENTRY 13, 45
IRQ_ENTRY 14, 46
IRQ_ENTRY 15, 47

extern irq_handler

irq_common_stub:
	pusha
	push ds
	push es
	push fs
	push gs
	mov ax, 0x10
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax
	mov eax, esp
	push eax
	; Call the C kernel hardware interrupt handler
	mov eax, irq_handler
	call eax
	pop eax
	pop gs
	pop fs
	pop es
	pop ds
	popa
	add esp, 8
	iret


; BSS Section
SECTION .bss
	resb 8192 ; 8KB of memory reserved
_sys_stack:
; This line intentionally left blank

