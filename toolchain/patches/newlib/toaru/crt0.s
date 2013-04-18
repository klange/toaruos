; ToAruOS User CRT0
BITS 32

global _start
_start:             ; Global entry point
	pop    eax      ; Our stack is slightly off
	extern pre_main     ;
	extern main
	push main
	call   pre_main     ; call C main function

; vim:syntax=nasm
; vim:noexpandtab
; vim:tabstop=4
; vim:shiftwidth=4
