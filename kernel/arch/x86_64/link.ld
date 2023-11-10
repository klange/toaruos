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
		*(.shit)
	}

	.rodata BLOCK(4K) : ALIGN(4K)
	{
		*(.rodata)
	}
	.rela.dyn BLOCK(4K) : ALIGN(4K)
	{
		_rela_start = .;
		*(.rela)
		*(.rela.text)
		*(.rela.got)
		*(.rela.plt)
		*(.rela.bss)
		*(.rela.ifunc)
		*(.rela.text.*)
		*(.rela.data)
		*(.rela.data.*)
		*(.rela.rodata)
		*(.rela.rodata*)
		*(.rela.dyn)
		_rela_end = .;
	}

	.data BLOCK(4K) : ALIGN(4K)
	{
		data = .;
		*(.data)
		*(.symbols)
		PROVIDE(kernel_symbols_start = .);
		PROVIDE(kernel_symbols_end = .);
	}

	.bss BLOCK(4K) : ALIGN(4K)
	{
		PROVIDE(bss_start = .);
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
