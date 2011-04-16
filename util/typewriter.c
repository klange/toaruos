#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char ** argv) {
	if (argc < 3) { return -1; }
	FILE * f = fopen(argv[2],"r");
	char * buffer = malloc(sizeof(char) * 1);
	while (!feof(f)) {
		fread(buffer, 1, 1, f);
		printf("%c", buffer[0]);
		fflush(stdout);
		usleep(atoi(argv[1]));
	}
	fclose(f);
	return 0;
}
