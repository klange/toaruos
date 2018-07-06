[bits 32]
global jump_to_main
jump_to_main:
	extern _eax
	extern _ebx
	extern _xmain
	mov eax, [_eax]
	mov ebx, [_ebx]
	jmp [_xmain]

