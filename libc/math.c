#include <math.h>

double exp(double x) {
	return __builtin_exp(x);
}

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

double pow(double x, double y) {
	return __builtin_pow(x,y);
}
