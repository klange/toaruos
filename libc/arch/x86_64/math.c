#include <math.h>

double floor(double x) {
	if (x == 0.0) return x;

	double out;
	asm volatile (
		"frndint\n"
		: "=t"(out) : "0"(x)
	);
	if (out > x) return out - 1.0;
	return out;
}

double pow(double x, double y) {
	double out;
	asm volatile (
		"fyl2x;"
		"fld %%st;"
		"frndint;"
		"fsub %%st,%%st(1);"
		"fxch;"
		"fchs;"
		"f2xm1;"
		"fld1;"
		"faddp;"
		"fxch;"
		"fld1;"
		"fscale;"
		"fstp %%st(1);"
		"fmulp;" : "=t"(out) : "0"(x),"u"(y) : "st(1)" );
	return out;
}

double fmod(double x, double y) {
	long double out;
	asm volatile (
		"1: fprem;" /* Partial remainder */
		"   fnstsw %%ax;" /* store status word */
		"   sahf;" /* store AX (^ FPU status) into flags */
		"   jp 1b;" /* jump back to 1 above if parity flag==1 */
		: "=t"(out) : "0"(x), "u"(y) : "ax", "cc");
	return out;
}

double tan(double x) {
	double out;
	double _x = x;
	double one;
	asm volatile (
		"fldl %2\n"
		"fptan\n"
		"fstpl %1\n"
		"fstpl %0\n"
		: "=m"(out), "=m"(one) : "m"(_x)
	);
	return out;
}

double atan2(double y, double x) {
	double out;
	double _x = x;
	double _y = y;
	asm volatile (
		"fldl %1\n"
		"fldl %2\n"
		"fpatan\n"
		"fstpl %0\n"
		: "=m"(out) : "m"(_y), "m"(_x)
	);
	return out;
}

double sqrt(double x) {
	/* This is what __builtin_sqrt was doing anyway? */
	asm volatile (
		"sqrtsd %1, %0\n"
		: "=x"(x) : "x"(x)
	);
	return x;
}

