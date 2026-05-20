.global _start
.type   _start, %function

_start:
	adr x3, main
	bl	pre_main
