#include <math.h>

double modf(double x, double *iptr) {
	int i = (int)x;
	*iptr = (double)i;
	return x - i;
}
