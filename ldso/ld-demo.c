#include <stdio.h>

extern int foo(int);

int main(int argc, char * argv[]) {
	puts("Hello world!\n");
	return foo(7);
}
