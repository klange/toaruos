#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>

int main(int argc, char * argv[]) {
#define p(sz) do { void * ptr = malloc(sz); fprintf(stderr, "%zu = %zu\n", (size_t)sz, malloc_usable_size(ptr)); free(ptr); } while (0)

	p(1);
	p(3);
	p(5);
	p(37);
	p(1234);
	p(235032);
}

