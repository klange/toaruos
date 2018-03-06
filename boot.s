[bits 16]
main:
	mov ax, 0x0000
	mov ds, ax
	mov ax, 0x0500
	mov es, ax

	cli

	; memory scan
	mov di, 0x0
	call do_e820
	jc hang

	; a20
	in al, 0x92
	or al, 2
	out 0x92, al

	; basic flat GDT
	xor eax, eax
	mov ax, ds
	shl eax, 4
	add eax, gdt_base
	mov [gdtr+2], eax
	mov eax, gdt_end
	sub eax, gdt_base
	mov [gdtr], ax
	lgdt [gdtr]

	; protected mode enable flag
	mov eax, cr0
	or eax, 1
	mov cr0, eax

	; set segments
	mov ax, 0x10
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax
	mov ss, ax

	; jump to protected mode entry
	extern kmain
	jmp far 0x08:(kmain)

hang:
	jmp hang

do_e820:
	xor ebx, ebx		; ebx must be 0 to start
	xor bp, bp		; keep an entry count in bp
	mov edx, 0x0534D4150	; Place "SMAP" into edx
	mov eax, 0xe820
	mov [es:di + 20], dword 1	; force a valid ACPI 3.X entry
	mov ecx, 24		; ask for 24 bytes
	int 0x15
	jc short .failed	; carry set on first call means "unsupported function"
	mov edx, 0x0534D4150	; Some BIOSes apparently trash this register?
	cmp eax, edx		; on success, eax must have been reset to "SMAP"
	jne short .failed
	test ebx, ebx		; ebx = 0 implies list is only 1 entry long (worthless)
	je short .failed
	jmp short .jmpin
.e820lp:
	mov eax, 0xe820		; eax, ecx get trashed on every int 0x15 call
	mov [es:di + 20], dword 1	; force a valid ACPI 3.X entry
	mov ecx, 24		; ask for 24 bytes again
	int 0x15
	jc short .e820f		; carry set means "end of list already reached"
	mov edx, 0x0534D4150	; repair potentially trashed register
.jmpin:
	jcxz .skipent		; skip any 0 length entries
	cmp cl, 20		; got a 24 byte ACPI 3.X response?
	jbe short .notext
	test byte [es:di + 20], 1	; if so: is the "ignore this data" bit clear?
	je short .skipent
.notext:
	mov ecx, [es:di + 8]	; get lower uint32_t of memory region length
	or ecx, [es:di + 12]	; "or" it with upper uint32_t to test for zero
	jz .skipent		; if length uint64_t is 0, skip entry
	inc bp			; got a good entry: ++count, move to next storage spot
	add di, 24
.skipent:
	test ebx, ebx		; if ebx resets to 0, list is complete
	jne short .e820lp
.e820f:
	mov [mmap_ent], bp	; store the entry count
	clc			; there is "jc" on end of list to this point, so the carry must be cleared
	ret
.failed:
	stc			; "function unsupported" error exit
	ret

align 8

; GDT pointer
gdtr
	dw 0
	dd 0

; GDT (null, code, data)
gdt_base
	; null
	dq 0
	; code
	dw 0xFFFF
	dw 0
	db 0
	db 0x9a
	db 0xcf
	db 0
	; data
	dw 0xffff
	dw 0
	db 0
	db 0x92
	db 0xcf
	db 0
gdt_end

; memory map entry count
global mmap_ent
mmap_ent db 0, 0

[bits 32]
global jump_to_main
jump_to_main:
	extern _eax
	extern _ebx
	extern _xmain
	mov eax, [_eax]
	mov ebx, [_ebx]
	jmp [_xmain]

