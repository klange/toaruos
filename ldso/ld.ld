ENTRY(_start)

SECTIONS {
	. = 0x3FA0000;
	__elf_start = .;
	. += SIZEOF_HEADERS;

	. = ALIGN(4096);
	.text : { *(.text*); }

	.rodata : { *(.rodata*); }

	.data : { *(.data*); }

	. = ALIGN(4096);
	.bss : { *(.bss*); }

	/DISCARD/ : { *(.comment); *(.interp) }
}
