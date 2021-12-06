#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

int main(int argc, char * argv[]) {
	if (argc < 2) return 1;

	union {
		double asFloat;
		uint64_t asInt;
	} bits;

	if (!strcmp(argv[1], "inf")) {
		bits.asFloat = INFINITY;
	} else if (!strcmp(argv[1], "nan")) {
		bits.asFloat = NAN;
	} else {
		bits.asFloat = strtod(argv[1], NULL);
	}

	printf("0x%016zx %d\n", bits.asInt, fpclassify(bits.asFloat));
	return 0;
}
