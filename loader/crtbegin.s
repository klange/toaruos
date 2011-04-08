BITS 32

global _start
_start:
	push   0 ; argc
	push   0 ; argv
	extern main
	call   main
	mov    ebx, eax ; return value from main
	mov    eax, 0x0
	int    0x7F
_wait:
	hlt
	jmp    _wait
