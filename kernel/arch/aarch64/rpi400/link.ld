OUTPUT_FORMAT(elf64-littleaarch64)
ENTRY(start)

SECTIONS
{
	. = 0x80000;
	phys = .;

	.text BLOCK(4K) : ALIGN(4K)
	{
		*(.bootstrap)
		code = .;
		*(.text)
	}

	.rodata BLOCK(4K) : ALIGN(4K)
	{
		*(.rodata)
	}

	.data BLOCK(4K) : ALIGN(4K)
	{
		data = .;
		*(.data)
		*(.symbols)
		PROVIDE(kernel_symbols_start = .);
		PROVIDE(kernel_symbols_end = .);
		PROVIDE(bss_start = .);
	}

	__bss_start = .;
	.bss BLOCK(4K) : ALIGN(4K)
	{
		bss = .;
		*(COMMON)
		*(.bss)
		*(.stack)
	}
	__bss_end = .;
	__bss_size = __bss_end - __bss_start;

	/* Some built-in stack space... */
	. = ALIGN(0x1000);
	. = . + 0x1000;
	__bootstrap_stack_top = .;

	end = .;

	/DISCARD/ :
	{
		*(.comment)
		*(.eh_frame)
		*(.note.gnu.build-id)
	}

}
