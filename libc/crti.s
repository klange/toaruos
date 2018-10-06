; ToAruOS User CRT0
BITS 32

section .init
global  _init
_init:
	push ebp
	; .init goes here

section .fini
global  _fini
_fini:
	push ebp
	; .fini goes here

; vim:syntax=nasm
; vim:noexpandtab
; vim:tabstop=4
; vim:shiftwidth=4
