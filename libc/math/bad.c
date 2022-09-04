/* STUB MATH LIBRARY */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

extern char * _argv_0;
#define BAD do { if (getenv("LIBM_DEBUG")) { fprintf(stderr, "%s called bad math function %s\n", _argv_0, __func__); } } while (0)

double acos(double x) {
	BAD;
	return 0.0;
}

double asin(double x) {
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

double log1p(double x) {
	BAD;
	return log(x+1.0);
}

double expm1(double x) {
	BAD;
	return exp(x) - 1.0;
}

double trunc(double x) {
	BAD;
	return (double)(long)x;
}

double acosh(double x) { BAD; return 0.0; }
double asinh(double x) { BAD; return 0.0; }
double atanh(double x) { BAD; return 0.0; }
double erf(double x)   { BAD; return 0.0; }
double erfc(double x)  { BAD; return 0.0; }
double gamma(double x) { BAD; return 0.0; }
double tgamma(double x){ BAD; return 0.0; }
double lgamma(double x){ BAD; return 0.0; }
double remainder(double x, double y) { BAD; return 0.0; }

double copysign(double x, double y) {
	if (y < 0) {
		if (x < 0) return x;
		return -x;
	} else {
		if (x < 0) return -x;
		return x;
	}
}

