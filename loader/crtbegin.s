BITS 32

global _start
_start:
	push   0 ; argc
	push   0 ; argv
	extern main
	call   main
	mov    eax, 0
	int    0x79
_wait:
	hlt
	jmp    _wait
