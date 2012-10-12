#include <stdio.h>
#include <stdlib.h>

int main(int argc, char ** argv) {
	if (argc > 1) {
		int i = atoi(argv[1]);
		if (i) {
			printf("\033[2001z");
		} else {
			printf("\033[2000z");
		}
		fflush(stdout);
	}
	return 0;
	return 0;
}
