#include <math.h>

double floor(double x) {
	return __builtin_floor(x);
}

int abs(int j) {
	return __builtin_abs(j);
}

double exp(double x) {
	return __builtin_exp(x);
}

#if 0
double floor(double x) {
	if (x > -1.0 && x < 1.0) {
		if (x >= 0) {
			return 0.0;
		} else {
			return -1.0;
		}
	}

	if (x < 0) {
		int x_i = x;
		return (double)(x_i - 1);
	} else {
		int x_i = x;
		return (double)x_i;
	}
}

int abs(int j) {
	return (j < 0 ? -j : j);
}
#endif
