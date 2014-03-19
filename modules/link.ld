SECTIONS
{
	.text :
	{
		*(.text*)
		*(.rodata)
	}
	.data :
	{
		*(.data*)
	}
	.bss :
	{
		*(.bss*)
	}
	.moddeps :
	{
		*(.moddeps)
	}
}
