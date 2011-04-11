/*
 * Userspace Application Linker Script
 * 2011, ToAruOS
 */
OUTPUT_FORMAT(elf32-i386)
ENTRY(_start)
STARTUP(loader/crtbegin.o)
INPUT(loader/syscall.o)
phys = 0x02000000;
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
	 * Get rid of unnecessary GCC bits.
	 */
	/DISCARD/ :
	{
		*(.comment)
		*(.eh_frame)
		*(.note.gnu.build-id)
	}
}
