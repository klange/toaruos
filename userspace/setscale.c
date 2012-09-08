#include <stdio.h>

int main(int argc, char ** argv) {
	printf("\033[1555;%sz", argv[1]);
	fflush(stdout);
	return 0;
}
