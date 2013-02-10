#include <stdio.h>
#include <stdint.h>

int main(int argc, char * argv[]) {
	printf("Shutting down...\n");
	/* Nothing to actually do for shutdown, sadly */
	__asm__ __volatile__ ("outw %1, %0" : : "dN" ((uint16_t)0xB004), "a" ((uint16_t)0x2000));
	return 0;
}
