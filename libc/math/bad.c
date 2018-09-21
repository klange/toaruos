/* STUB MATH LIBRARY */
#include <stdio.h>
#include <stdlib.h>

#define BAD do { if (getenv("LIBM_DEBUG")) { fprintf(stderr, "Called bad math function %s\n", __func__); } } while (0)

double acos(double x) {
	BAD;
	return 0.0;
}

double asin(double x) {
	BAD;
	return 0.0;
}

double ceil(double x) {
	BAD;
	return 0.0; /* extract and convert? */
}

double cosh(double x) {
	BAD;
	return 0.0;
}

double ldexp(double a, int exp) {
	double out = a;
	while (exp) {
		out *= 2.0;
		exp--;
	}
	return out;
}

double log(double x) {
	BAD;
	return 0.0;
}

double log10(double x) {
	BAD;
	return 0.0;
}

double log2(double x) {
	BAD;
	return 0.0;
}

double sinh(double x) {
	BAD;
	return 0.0;
}

double tanh(double x) {
	BAD;
	return 0.0;
}

