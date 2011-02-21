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
.global _readn
_start: 
        movw $0x00,%ax   # Initialize segments...
        movw %ax,%ds     # ... data
        movw %ax,%es     # ... e?
        movw %ax,%ss     # ... selector

        movw $0x7bf0,%sp # stack pointer

        movw $bmsga, %si # Print "Starting..."
        calll _print     # ...

        .extern main     # Jump the C main entry point...
        calll main

        movw $bmsgb, %si # We should not be here...
        calll _print     # ...
        hlt

_readn:
        movw $0x0000, %ax # We are segment 0...
        movw %ax, %es
        movw $0x7e00, %bx # Address to put output
        movb $0x01, %al   # Count
        movb $0x00, %ch   # Track
        movb $0x02, %cl   # Sector
        movb $0x00, %dh   # Head
        movb $0x80, %dl   # Disk
        movb $0x02, %ah   # Command number
        int  $0x13
        retl

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
        .string "Critical failure!\r\n"
