OUTPUT_FORMAT(binary)
ENTRY(_start)
load = 0x7e00;
phys = 0x0;
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
	/DISCARD/ :
	{
		*(.bss)
		*(.comment)
		*(.eh_frame)
		*(.note.gnu.build-id)
	}
}
