#
# Mr. Boots - Stage 2 (ASM entry point)
#
.code16

.text

.global _start
.global _print
_start: 
        movw $0x00,%ax   # Initialize segments...
        movw %ax,%ds     # ... data
        movw %ax,%es     # ... e?
        movw %ax,%ss     # ... selector

        movw $0x7bf0,%sp # stack pointer

        .extern main     # Jump the C main entry point...
        jmp main

_print: 
        movb $0x0E,%ah # set registers for
        movb $0x00,%bh # bios video call
        movb $0x07,%bl
_print.next: 
        lodsb          # string stuff
        orb %al,%al    # if 0...
        jz _print.return # return
        int $0x10      # print character
        jmp _print.next # continue
_print.return: 
        retl
