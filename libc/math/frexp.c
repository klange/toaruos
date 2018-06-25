#include <string.h>
#include <stdint.h>

double frexp(double x, int *exp) {
	struct {
		uint32_t lsw;
		uint32_t msw;
	} extract;

	memcpy(&extract, &x, sizeof(double));

	*exp = ((extract.msw & 0x7ff00000) >> 20) - 0x3FE;

	struct {
		uint32_t lsw;
		uint32_t msw;
	} out_double;

	out_double.msw = (extract.msw & 0x800fffff) | 0x3FE00000;
	out_double.lsw = extract.lsw;

	double out;
	memcpy(&out, &out_double, sizeof(double));
	return out;
}
