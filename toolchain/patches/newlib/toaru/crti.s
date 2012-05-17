; ToAruOS User CRT0
BITS 32

global _start
_start:             ; Global entry point
	pop    eax      ; Our stack is slightly off
	extern __do_global_ctors
	call __do_global_ctors
	extern main     ;
	call   main     ; call C main function
	mov    ebx, eax ; return value from main
	mov    eax, 0x0 ; sys_exit
	int    0x7F     ; syscall
_wait:              ; wait until we've been deschuled
	hlt
	jmp    _wait

; vim:syntax=nasm
; vim:noexpandtab
; vim:tabstop=4
; vim:shiftwidth=4
