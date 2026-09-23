#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int test_case(const char * expected, const char * fmt, double val) {
	int ret;
	char *a;
	asprintf(&a, fmt, val);

	if ((ret = strcmp(a, expected))) {
		fprintf(stderr, "bad:  '%s' != '%s' (\"%s\")\n", a, expected, fmt);
	} else {
		fprintf(stderr, "good: '%s'\n", a);
	}

	free(a);
	return ret;
}

int main(int argc, char * argv[]) {
	int ret = 0;

	ret |= test_case("0.500000", "%f", 0.5);
	ret |= test_case("    0.500000", "%12f", 0.5);
	ret |= test_case("-0.500000", "%f", -0.5);
	ret |= test_case("   -0.500000", "%12f", -0.5);
	ret |= test_case("-0.500000   x", "%-12fx", -0.5);
	ret |= test_case("-0000.500000x", "%012fx", -0.5);
	ret |= test_case("        -infx", "%012fx", -__builtin_inf());
	ret |= test_case("         nanx", "%012fx", -__builtin_nan(""));
	ret |= test_case("0.5", "%g", 0.5);
	ret |= test_case("-0.5", "%g", -0.5);
	ret |= test_case("5.000000e-01", "%e", 0.5);
	ret |= test_case("3.252532e+03", "%e", 3252.5325);
	ret |= test_case("5424999999999999684148494255290540640040801781099195554604724143641530192862270159177076046989983905480704.000000", "%f", 5.425e105);
	ret |= test_case("5.425e+105", "%g", 5.425e105);
	ret |= test_case("-0.500000000000", "%.12f", -0.5);
	ret |= test_case(" 0.500000", "% f", 0.5);
	ret |= test_case("+0.500000", "%+f", 0.5);
	ret |= test_case("-0.500000", "% f", -0.5);
	ret |= test_case("-0.500000", "%+f", -0.5);
	ret |= test_case(" 5.000000e+00", "% e", 5.0);
	ret |= test_case("1.000125", "%f", 1.000125);

	return ret;
}
