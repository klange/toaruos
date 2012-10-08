#include <stdio.h>
#include <stdlib.h>

int main(int argc, char * argv) {
	printf("hello world\n");
	char * term = getenv("TERM");
	if (term) {
		printf("TERM=%s\n", term);
	} else {
		printf("TERM is not set.\n");
	}
	return 0;
}
