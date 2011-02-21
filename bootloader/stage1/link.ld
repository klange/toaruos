OUTPUT_FORMAT(binary)
/*
OUTPUT_FORMAT(elf32-i386)
*/
ENTRY(_start)
load = 0x7c00;
phys = 0x0;
boot = 0x1FE;
SECTIONS
{
	.text load : AT(phys)
	{
		*(.text)
		. = ALIGN(4);
	}
	.data : AT(phys + SIZEOF(.text))
	{
		*(.data)
		. = ALIGN(4);
	}
	.rodata : AT (phys + SIZEOF(.text) + SIZEOF(.data))
	{
		*(.rodata)
	}
	.bootsig : AT(boot)
	{
		SHORT(0xaa55)
	}
	/DISCARD/ :
	{
		*(.bss)
		*(.comment)
		*(.eh_frame)
		*(.note.gnu.build-id)
	}
}
