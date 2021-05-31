OUTPUT_FORMAT(elf64-x86-64)
ENTRY(start)

SECTIONS
{
	. = 1M;
	phys = .;

	.text BLOCK(4K) : ALIGN(4K)
	{
		*(.multiboot)
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

	.bss BLOCK(4K) : ALIGN(4K)
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
