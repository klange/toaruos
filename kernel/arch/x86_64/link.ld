OUTPUT_FORMAT(elf64-x86-64)
ENTRY(start)

SECTIONS
{
	. = 1M;
	__ehdr_start = .;
	. = 1M + SIZEOF_HEADERS;
	phys = .;

	.text BLOCK(4K) : ALIGN(4K)
	{
		*(.multiboot)
		*(.bootstrap)
		code = .;
		*(.text)
		*(.shit)
	} :text

	.rodata BLOCK(4K) : ALIGN(4K)
	{
		*(.rodata)
	} :text

	.dynsym : {
		KEEP(*(.dynsym))
		__dynsym_end = .;
	} :text

	.dynstr : {
		KEEP(*(.dynstr))
	} :text

	.dynamic BLOCK(4K) : ALIGN(4K)
	{
		*(.dynamic)
	} :data :dynamic

	.data BLOCK(4K) : ALIGN(4K)
	{
		data = .;
		*(.data)
		PROVIDE(bss_start = ALIGN(4K));
	} :data

	.bss BLOCK(4K) : ALIGN(4K)
	{
		bss = .;
		*(COMMON)
		*(.bss)
		*(.stack)
	} :data

	end = .;

	/DISCARD/ :
	{
		*(.comment)
		*(.eh_frame)
		*(.note.gnu.build-id)
	}

}

PHDRS {
	text PT_LOAD FILEHDR PHDRS;
	data PT_LOAD;
	dynamic PT_DYNAMIC;
}
