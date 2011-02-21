/*
 * Mr. Boots Stage 2
 */

/*
 * 16-bit bootloader
 */
__asm__(".code16\n");

/*
 * Main entry point
 */
int main() {
	return 0;
}

void print(char * str) {
	__asm__ ("movb $0x0E, %ah\n"
			 "movb $0x00, %bh\n"
			 "movb $0x07, %bl\n"
			);
	int i = 0;
	while (str[i] != '\0') {
		__asm__ __volatile__ ("movb %0, %%al\n"
							  "int $0x10" : : "m" (str[i]));
		++i;
	}
}
