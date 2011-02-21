# Mr. Boots - Stage 1
# Find Stage 2 and immediately load it.
#
# NOTICE: This stage should be loaded from a partition on
#         an EXT2-only disk without a partition table.
#         If you want to use it with a different set up
#         you need to patch it to include an MBR header
#         and all the other necessary bits.
#
# Part of the ToAruOS Distribution of the ToAru Kernel
#
# NCSA license is available from the root directory of the
# source tree in which this file is shipped.
#
#
.code16

.text

.global _start
.global _print
_start: 
        movw $0x00,%ax # Initialize data segment
        movw %ax,%ds   # ...
        movw %ax,%es
        movw %ax,%ss
        movw $0x7bf0,%sp # stack

        #mov ax, 0x4F02 ; VESA function "Set Video Mode"
        #mov bx, 0x0107 ; 1024x768x256
        #int 0x10       ; BIOS video interrupt

        movw $bmsga, %si # Print "Starting..."
        calll _print     # ...

        .extern main
        calll main

        movw $bmsgb, %si # Print ":("
        calll _print     # ...
        # uh...
        hlt

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

.data

bmsga:
        .string "Starting up...\r\n"
bmsgb:
        .string ":(\r\n"
