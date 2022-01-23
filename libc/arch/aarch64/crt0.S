.global _start
.type   _start, %function

_start:
	mov x4, x0
	adr x0, main
	mov x3, x0
	mov x0, x4
	bl	pre_main
