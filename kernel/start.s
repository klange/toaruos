; ToAruOS Start Up / Entry Point
;
; This file is part of ToaruOS and is released under the terms
; of the NCSA / University of Illinois License - see LICENSE.md
; Copyright (C) 2013-2014 Kevin Lange
;
BITS 32
ALIGN 4


SECTION .multiboot
mboot:
	; Multiboot headers:
	;   Page aligned loading, please
	MULTIBOOT_PAGE_ALIGN	equ 1<<0
	;   We require memory information
	MULTIBOOT_MEMORY_INFO	equ 1<<1
	;   We would really, really like graphics...
	MULTIBOOT_USE_GFX		equ 1<<2
	;   We are multiboot compatible!
	MULTIBOOT_HEADER_MAGIC	equ 0x1BADB002
	;   Load up those flags.
	MULTIBOOT_HEADER_FLAGS	equ MULTIBOOT_PAGE_ALIGN | MULTIBOOT_MEMORY_INFO | MULTIBOOT_USE_GFX
	;   Checksum the result
	MULTIBOOT_CHECKSUM		equ -(MULTIBOOT_HEADER_MAGIC + MULTIBOOT_HEADER_FLAGS)
	; Load the headers into the binary image.
	dd MULTIBOOT_HEADER_MAGIC
	dd MULTIBOOT_HEADER_FLAGS
	dd MULTIBOOT_CHECKSUM
	dd 0x00000000 ; header_addr
	dd 0x00000000 ; load_addr
	dd 0x00000000 ; load_end_addr
	dd 0x00000000 ; bss_end_addr
	dd 0x00000000 ; entry_addr
	; Graphics requests
	dd 0x00000000 ; 0 = linear graphics
	dd 0
	dd 0
	dd 32         ; Set me to 32 or else.


SECTION .text
; Some external references.
extern code, bss, end

; Main entrypoint
global start
start:
	; Set up stack pointer.
	mov esp, 0x7FFFF
	push esp
	; Push the incoming mulitboot headers
	push eax ; Header magic
	push ebx ; Header pointer
	; Disable interrupts
	cli
	; Call the C entry
	extern	kmain
	call	kmain
	jmp		$


; Global Descriptor Table
global gdt_flush
extern gp
gdt_flush:
	; Load the GDT
	lgdt [gp]
	; Flush the values to 0x10
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

; Return to Userspace (from thread creation)
global return_to_userspace
return_to_userspace:
	pop gs
	pop fs
	pop es
	pop ds
	popa
	add esp, 8
	iret

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

%macro IRQ_ENTRY 2
	global _irq%1
	_irq%1:
		cli
		push byte 0
		push byte %2
		jmp irq_common_stub
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
ISR_NOERR 127


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

; Interrupt handlers
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

global read_eip
read_eip: ; Clever girl
	pop eax
	jmp eax

global copy_page_physical
copy_page_physical:
    push ebx
    pushf
    cli
    mov ebx, [esp+12]
    mov ecx, [esp+16]
    mov edx, cr0
    and edx, 0x7FFFFFFF
    mov cr0, edx
    mov edx, 0x400
.page_loop:
    mov eax, [ebx]
    mov [ecx], eax
    add ebx, 4
    add ecx, 4
    dec edx
    jnz .page_loop
    mov edx, cr0
    or  edx, 0x80000000
    mov cr0, edx
    popf
    pop ebx
    ret

global tss_flush
tss_flush:
	mov ax, 0x2B
	ltr ax
	ret

; BSS Section
SECTION .bss
	resb 8192 ; 8KB of memory reserved

