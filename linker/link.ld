ENTRY(_start)

SECTIONS
{
	. = 0x3F000000;
	phys = .;

	.text BLOCK(4K) : ALIGN(4K)
	{
		code = .;
		*(.text)
	}

	.rodata BLOCK(4K) : ALIGN(4K)
	{
		*(.rodata)
	}

	.data BLOCK(4K) : ALIGN(4K)
	{
		*(.data)
	}

	.bss BLOCK(4K) : ALIGN(4K)
	{
		*(COMMON)
		*(.bss)
	}

	.eh_frame BLOCK(4K) : ALIGN(4K) {
		*(.eh_frame)
	}

	.init_array : {
		PROVIDE_HIDDEN (__init_array_start = .);
		*(.init_array);
		PROVIDE_HIDDEN (__init_array_end = .);
	}

	end = .;

	/DISCARD/ :
	{
		*(.comment)
		*(.note.gnu.build-id)
	}
}
