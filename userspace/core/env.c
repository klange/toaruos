#include <stdio.h>

int main(int argc, char ** argv) {
	unsigned int x = 0;
	unsigned int nulls = 0;
	for (x = 0; 1; ++x) {
		if (!argv[x]) {
			++nulls;
			if (nulls == 2) {
				break;
			}
			continue;
		}
		if (nulls == 1) {
			printf("%s\n", argv[x]);
		}
	}
}
