[BITS 32]
global start
start:
	mov esp, _sys_stack
	jmp stublet

ALIGN 4
mboot:
	MULTIBOOT_PAGE_ALIGN	equ 1<<0
	MULTIBOOT_MEMORY_INFO	equ 1<<1
	MULTIBOOT_AOUT_KLUDGE	equ 1<<16
	MULTIBOOT_HEADER_MAGIC	equ 0x1BADB002
	MULTIBOOT_HEADER_FLAGS	equ MULTIBOOT_PAGE_ALIGN | MULTIBOOT_MEMORY_INFO | MULTIBOOT_AOUT_KLUDGE
	MULTIBOOT_CHECKSUM		equ -(MULTIBOOT_HEADER_MAGIC + MULTIBOOT_HEADER_FLAGS)
	EXTERN code, bss, end
	; GRUB Multiboot header, boot signature
	dd MULTIBOOT_HEADER_MAGIC
	dd MULTIBOOT_HEADER_FLAGS
	dd MULTIBOOT_CHECKSUM
	; AOUT kludge (must be physical addresses)
	; Linker script fills these in
	dd mboot
	dd code
	dd bss
	dd end
	dd start

; Main entrypoint
stublet:
	extern	main
	call	main
	jmp		$

; GDT

; Interrupt Service Routines

; BSS Section
SECTION .bss
	resb 8192 ; 8KB of memory reserved
_sys_stack:
; This line intentionally left blank


