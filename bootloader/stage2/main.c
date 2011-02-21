/*
 * Mr. Boots Stage 1
 */

/*
 * 16-bit bootloader
 */
__asm__(".code16gcc\n");

#define PRINT(s) __asm__ ("movw %0, %%si\ncall _print" : : "l"((short)(int)s))

/*
 * Main entry point
 */
void main()
{
	PRINT("== Mr. Boots Stage 2 Bootloader ==\r\n");

	/* And that's it for now... */
	__asm__ __volatile__ ("hlt");
	while (1) {};
}
