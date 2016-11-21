#include <stdio.h>

extern int return_42(void);

int main(int argc, char * argv[]) {
	fprintf(stderr, "Hello, dynamic world: %d\n", return_42());

	return 0;
}
