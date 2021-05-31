.global _init
.section .init
_init:
	push %rbp
	movq %rsp, %rbp

.global _fini
.section .fini
_fini:
	push %rbp
	movq %rsp, %rbp
