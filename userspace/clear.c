#include <stdio.h>

int main(int argc, char ** argv) {
	printf("\033[H\033[2J");
	fflush(stdout);
	return 0;
}
