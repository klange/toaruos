.global _start
.type   _start, %function

_start:
	adr x3, main
	bl	__libc_start_main
