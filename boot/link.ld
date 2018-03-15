/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * Kernel linker script for x86
 */
OUTPUT_FORMAT("binary")
/*ENTRY(start) */
phys = 0x7c00;

SECTIONS
{
	. = 0x7c00;

	.text : 
	{
		code = .;
		*(.text)
	}

	.rodata BLOCK(1) : ALIGN(1)
	{
		*(.rodata)
	}

	.data BLOCK(1) : ALIGN(1)
	{
		data = .;
		*(.data)
	}

	.bss BLOCK(1) : ALIGN(1)
	{
		bss = .;
		*(COMMON)
		*(.bss)
		*(.stack)
	}

	end = .;

	/DISCARD/ :
	{
		*(.comment)
		*(.eh_frame)
		*(.note.gnu.build-id)
	}
}
