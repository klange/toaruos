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
	.padding : AT (phys + SIZEOF(.text) + SIZEOF(.data) + SIZEOF(.rodata))
	{
		/* Pad so we have at least one sector */
		. = ALIGN(4096);
		/* Add an additional sector for good measure */
		. = ALIGN(508);
		/* End it with a sad face */
		SHORT(0xada5);
		SHORT(0xcefa);
	}
	/DISCARD/ :
	{
		*(.bss)
		*(.comment)
		*(.eh_frame)
		*(.note.gnu.build-id)
	}
}
