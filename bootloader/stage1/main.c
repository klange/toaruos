/*
 * Mr. Boots Stage 1 Main
 */

/*
 * 16-bit bootloader
 */
__asm__(".code16gcc\n");

#define PRINT(s) __asm__ ("movw %0, %%si\ncall _print" : : "l"((short)(int)s))

void read(unsigned char count, unsigned char sector, short segment, short offset)
{
	__asm__ __volatile__ (
			"movw %0, %%es\n"
			"movw %1, %%bx\n"
			"movb %2, %%al\n"
			"movb $0, %%ch\n"
			"movb %3, %%cl\n"
			"movb $0, %%dh\n"
			"movb $0x00, %%dl\n"
			"movb $0x02, %%ah\n"
			"int $0x13" : :
			"l" (segment),
			"l" (offset),
			"m" (count),
			"m" (sector));
}

/*
 * Main entry point
 */
void main()
{
	PRINT("Loading... ");
	read(6,2,0,0x7e00);
	PRINT("Ready.\r\n");

	/* Let's do this... */
	__asm__ __volatile__ ("jmp $0x00, $0x7e00");
}
