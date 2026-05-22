OUTPUT_FORMAT(elf64-littleaarch64)
ENTRY(start)

SECTIONS
{
	. = 0xffffffff80000000;
	__ehdr_start = .;
	. = 0xffffffff80000000 + SIZEOF_HEADERS;
	phys = .;

	.text BLOCK(4K) : ALIGN(4K)
	{
		*(.bootstrap)
		code = .;
		*(.text)
	} :text

	.dynsym : {
		KEEP(*(.dynsym))
		__dynsym_end = .;
	} :text

	.dynstr : {
		KEEP(*(.dynstr))
	} :text

	.rodata BLOCK(4K) : ALIGN(4K)
	{
		*(.rodata)
	} :text

	.dynamic BLOCK(4K) : ALIGN(4K)
	{
		*(.dynamic)
	} :data :dynamic

	.data BLOCK(4K) : ALIGN(4K)
	{
		data = .;
		*(.data)
	} :data

	.bss BLOCK(4K) : ALIGN(4K)
	{
		PROVIDE(bss_start = .);
		bss = .;
		*(COMMON)
		*(.bss)
		*(.stack)

		/* Some built-in stack space... */
		. = ALIGN(0x1000);
		. = . + 0x4000;
		__bootstrap_stack_top = .;
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
	dynamic  PT_DYNAMIC;
}
