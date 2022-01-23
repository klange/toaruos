.global _init
.section .init
_init:
	stp x29,x30,[sp,-16]!
	mov x29,sp

.global _fini
.section .fini
_fini:
	stp x29,x30,[sp,-16]!
	mov x29,sp

