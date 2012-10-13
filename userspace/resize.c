#include <stdio.h>

int main(int argc, char ** argv) {
	if (argc < 3) return 1;
	printf("\033[3000;%s;%sz", argv[1], argv[2]);
	fflush(stdout);
	return 0;
}
