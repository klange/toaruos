/* bad math */
#include <math.h>

double sqrt(double x) {
	asm volatile ("fsqrt %d0, %d1" : "=w"(x) : "w"(x));
	return x;
}

double tan(double theta) {
	return sin(theta) / cos(theta);
}

double atan2(double y, double x) {
	return 0.0;
}

double pow(double x, double y) {
	return 0.0;
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
