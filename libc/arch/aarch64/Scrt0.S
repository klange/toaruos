.global _start
.type   _start, %function

_start:
	adrp x3, :got:main
	ldr     x3, [x3, #:got_lo12:main]
	bl	__libc_start_main

