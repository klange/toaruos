/*
 * Mr. Boots Stage 1 Main
 */

/*
 * 16-bit bootloader
 */
__asm__(".code16gcc\n");

#define PRINT(s) __asm__ ("movw %0, %%si\ncall _print" : : "l"((short)(int)s))

void read(unsigned short count, unsigned short sector, unsigned short segment, unsigned short offset)
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
	read(0x18, 0x68, 0x1000, 0x0);
	read(0x14c, 0x82, 0x1000, 0x3000);
	PRINT("Ready.\r\n");

	/* Let's do this... */
	__asm__ __volatile__ ("jmpl $0x1, $0x0");
	PRINT("I definitely went somewhere...\r\n");

}
