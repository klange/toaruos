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

/*
 * Main entry point
 */
int main() {
	char s[] = "Herp Derp\r\n";
	__asm__ __volatile__ ("movw %0, %%si\n"
						  "call _print" : : "l" ((short)(int)s));
	kprint((short)(int)"Derp\r\n");
	kprint((short)(int)"Herp\r\n");
	//while (1) {};
	return 0;
}
