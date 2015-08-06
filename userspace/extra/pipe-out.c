#include <stdio.h>
#include <stdint.h>

int main(int argc, char * argv[]) {
	if (argc < 2) {
		return 1;
	}

	FILE * out = fopen(argv[1], "w");

	while (!feof(stdin)) {
		char buf[1024];
		size_t r = fread(buf, 1, 1024, stdin);
		fwrite(buf, 1, r, out);
	}
	fflush(out);
	fclose(out);
	return 0;
}
