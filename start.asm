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
global _isr0
global _isr1
global _isr2
global _isr3
global _isr4
global _isr5
global _isr6
global _isr7
global _isr8
global _isr9
global _isr10
global _isr11
global _isr12
global _isr13
global _isr14
global _isr15
global _isr16
global _isr17
global _isr18
global _isr19
global _isr20
global _isr21
global _isr22
global _isr23
global _isr24
global _isr25
global _isr26
global _isr27
global _isr28
global _isr29
global _isr30
global _isr31

;  0: Divide By Zero Exception
_isr0:
	cli
	push byte 0
	push byte 0
	jmp isr_common_stub

;  1: Debug Exception
_isr1:
	cli
	push byte 0
	push byte 1
	jmp isr_common_stub

;  2: Non Maskable Interrupt Exception
_isr2:
	cli
	push byte 0
	push byte 2
	jmp isr_common_stub

;  3: Int 3 Exception
_isr3:
	cli
	push byte 0
	push byte 3
	jmp isr_common_stub

;  4: INTO Exception
_isr4:
	cli
	push byte 0
	push byte 4
	jmp isr_common_stub

;  5: Out of Bounds Exception
_isr5:
	cli
	push byte 0
	push byte 5
	jmp isr_common_stub

;  6: Invalid Opcode Exception
_isr6:
	cli
	push byte 0
	push byte 6
	jmp isr_common_stub

;  7: Coprocessor Not Available Exception
_isr7:
	cli
	push byte 0
	push byte 7
	jmp isr_common_stub

;  8: Double Fault Exception (With Error Code!)
_isr8:
	cli
	push byte 8
	jmp isr_common_stub

;  9: Coprocessor Segment Overrun Exception
_isr9:
	cli
	push byte 0
	push byte 9
	jmp isr_common_stub

; 10: Bad TSS Exception (With Error Code!)
_isr10:
	cli
	push byte 10
	jmp isr_common_stub

; 11: Segment Not Present Exception (With Error Code!)
_isr11:
	cli
	push byte 11
	jmp isr_common_stub

; 12: Stack Fault Exception (With Error Code!)
_isr12:
	cli
	push byte 12
	jmp isr_common_stub

; 13: General Protection Fault Exception (With Error Code!)
_isr13:
	cli
	push byte 13
	jmp isr_common_stub

; 14: Page Fault Exception (With Error Code!)
_isr14:
	cli
	push byte 14
	jmp isr_common_stub

; 15: Reserved Exception
_isr15:
	cli
	push byte 0
	push byte 15
	jmp isr_common_stub

; 16: Floating Point Exception
_isr16:
	cli
	push byte 0
	push byte 16
	jmp isr_common_stub

; 17: Alignment Check Exception
_isr17:
	cli
	push byte 0
	push byte 17
	jmp isr_common_stub

; 18: Machine Check Exception
_isr18:
	cli
	push byte 0
	push byte 18
	jmp isr_common_stub

; 19: Reserved
_isr19:
	cli
	push byte 0
	push byte 19
	jmp isr_common_stub

; 20: Reserved
_isr20:
	cli
	push byte 0
	push byte 20
	jmp isr_common_stub

; 21: Reserved
_isr21:
	cli
	push byte 0
	push byte 21
	jmp isr_common_stub

; 22: Reserved
_isr22:
	cli
	push byte 0
	push byte 22
	jmp isr_common_stub

; 23: Reserved
_isr23:
	cli
	push byte 0
	push byte 23
	jmp isr_common_stub

; 24: Reserved
_isr24:
	cli
	push byte 0
	push byte 24
	jmp isr_common_stub

; 25: Reserved
_isr25:
	cli
	push byte 0
	push byte 25
	jmp isr_common_stub

; 26: Reserved
_isr26:
	cli
	push byte 0
	push byte 26
	jmp isr_common_stub

; 27: Reserved
_isr27:
	cli
	push byte 0
	push byte 27
	jmp isr_common_stub

; 28: Reserved
_isr28:
	cli
	push byte 0
	push byte 28
	jmp isr_common_stub

; 29: Reserved
_isr29:
	cli
	push byte 0
	push byte 29
	jmp isr_common_stub

; 30: Reserved
_isr30:
	cli
	push byte 0
	push byte 30
	jmp isr_common_stub

; 31: Reserved
_isr31:
	cli
	push byte 0
	push byte 31
	jmp isr_common_stub

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

global _irq0
global _irq1
global _irq2
global _irq3
global _irq4
global _irq5
global _irq6
global _irq7
global _irq8
global _irq9
global _irq10
global _irq11
global _irq12
global _irq13
global _irq14
global _irq15

_irq0:
	cli
	push byte 0
	push byte 32
	jmp irq_common_stub

_irq1:
	cli
	push byte 0
	push byte 33
	jmp irq_common_stub

_irq2:
	cli
	push byte 0
	push byte 34
	jmp irq_common_stub

_irq3:
	cli
	push byte 0
	push byte 35
	jmp irq_common_stub

_irq4:
	cli
	push byte 0
	push byte 36
	jmp irq_common_stub

_irq5:
	cli
	push byte 0
	push byte 37
	jmp irq_common_stub

_irq6:
	cli
	push byte 0
	push byte 38
	jmp irq_common_stub

_irq7:
	cli
	push byte 0
	push byte 39
	jmp irq_common_stub

_irq8:
	cli
	push byte 0
	push byte 40
	jmp irq_common_stub

_irq9:
	cli
	push byte 0
	push byte 41
	jmp irq_common_stub

_irq10:
	cli
	push byte 0
	push byte 42
	jmp irq_common_stub

_irq11:
	cli
	push byte 0
	push byte 43
	jmp irq_common_stub

_irq12:
	cli
	push byte 0
	push byte 44
	jmp irq_common_stub

_irq13:
	cli
	push byte 0
	push byte 45
	jmp irq_common_stub

_irq14:
	cli
	push byte 0
	push byte 46
	jmp irq_common_stub

_irq15:
	cli
	push byte 0
	push byte 47
	jmp irq_common_stub

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


