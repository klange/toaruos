/*
 * Mr. Boots Stage 2
 */

/*
 * 16-bit bootloader
 */
__asm__(".code16gcc\n");

void kprint(short s)
{
	__asm__ __volatile__ ("movw %0, %%si\n"
						  "call _print" : : "l" ((short)s));
}

void read(unsigned char count, unsigned char sector, short segment, short offset)
{
	__asm__ __volatile__ (
			"movw %0, %%es\n"
			"movw %1, %%bx\n"
			"movb %2, %%al\n"
			"movb $0, %%ch\n"
			"movb %3, %%cl\n"
			"movb $0, %%dh\n"
			"movb $0x80, %%dl\n"
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
int main() {
	kprint((short)(int)"Welcome to C!\r\n");

	read(1,3,0,0x7e00);

	kprint((short)(int)"Contents of 0x7e00:\r\n");
	kprint(0x7e00);
	kprint((short)(int)"[end]\r\n");



	/* And that's it for now... */
	__asm__ __volatile__ ("hlt");
	while (1) {};

	/* Uh oh */
	return -1;
}
