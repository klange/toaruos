#
# Mr. Boots - Stage 1 (ASM entry point)
#
.code16

.text

.global _start
_start: 
        movw $0x00,%ax   # Initialize segments...
        movw %ax,%ds     # ... data

        movw $0x7bf0,%sp # stack pointer

        .extern main     # Jump the C main entry point...
        jmp main
