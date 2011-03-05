/*
 * Kernel linker script for x86
 */
OUTPUT_FORMAT(elf32-i386)
ENTRY(start)
phys = 0x00100000;
SECTIONS
{
	/*
	 * Actual code
	 */
	.text phys : AT(phys) {
		code = .;
		*(.text)
		*(.rodata)
		. = ALIGN(4096);
	}
	/*
	 * Kernel data
	 */
	.data : AT(phys + (data - code))
	{
		data = .;
		*(.data)
		. = ALIGN(4096);
	}
	/*
	 * Statically defined, uninitialized values
	 */
	.bss : AT(phys + (bss - code))
	{
		bss = .;
		*(.bss)
		. = ALIGN(4096);
	}
	/*
	 * End of kernel.
	 */
	end = .;
	/*
	 * Get rid of unnecessary GCC bits.
	 */
	/DISCARD/ :
	{
		*(.comment)
		*(.eh_frame)
		*(.note.gnu.build-id)
	}
}
