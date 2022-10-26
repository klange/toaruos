/* bad math */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

double sqrt(double x) {
	asm volatile ("fsqrt %d0, %d1" : "=w"(x) : "w"(x));
	return x;
}

double tan(double theta) {
	return sin(theta) / cos(theta);
}

/**
 * Polynomial approximation of arctangent
 *
 * @see https://www.dsprelated.com/showarticle/1052.php
 */
static inline double _atan(double z) {;
	double n1 = 0.97239411;
	double n2 = -0.19194795;
	return (n1 + n2 * z * z) * z;
}
double atan2(double y, double x) {
	if (x != 0.0) {
		if (fabs(x) > fabs(y)) {
			double z = y / x;
			if (x > 0.0) return _atan(z);
			else if (y >= 0.0) return _atan(z) + M_PI;
			else return _atan(z) - M_PI;
		} else {
			double z = x / y;
			if (y > 0.0) return -_atan(z) + M_PI/2.0;
			else return -_atan(z) - M_PI/2.0;
		}
	} else {
		if (y > 0.0) return M_PI/2.0;
		else if (y < 0.0) return -M_PI/2.0;
	}
	return 0.0;
}

double pow(double x, double y) {
	if (getenv("LIBM_DEBUG")) {
		fprintf(stderr, "pow(%f, %f)\n", x, y);
	}
	return x;
}

double fmod(double x, double y) {
	int _x = fpclassify(x);
	int _y = fpclassify(y);

	if (_y == FP_NAN) return NAN;
	if (_x == FP_INFINITE) return NAN;
	if (_y == FP_ZERO) return NAN;
	if (_x == FP_ZERO) return x;

	long div = x / y;
	return x - (double)div * y;
}
