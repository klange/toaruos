SECTIONS
{
	.text :
	{
		*(.text*)
		*(.rodata)
		. = ALIGN(4096);
	}
	.data :
	{
		*(.data*)
		. = ALIGN(4096);
	}
	.bss :
	{
		*(.bss*)
		. = ALIGN(4096);
	}
}
