#include <stdio.h>
#include <string.h>

int main(int argc, char * argv[]) {
	if (argc > 1) {
		fprintf(stderr, "%s\n", (char*)(uintptr_t)strtoul(argv[1],NULL,0));
	} else {
		*(volatile int*)0x12345 = 42;
	}
	return 0;
}
