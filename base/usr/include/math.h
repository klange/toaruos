#pragma once

#include <_cheader.h>

_Begin_C_Header

#define M_PI 3.14159265358979323846
#define M_E  2.7182818284590452354
#define NAN (__builtin_nanf(""))
#define INFINITY (__builtin_inff())

extern double floor(double x);
extern int abs(int j);
extern double pow(double x, double y);
extern double exp(double x);
extern double fmod(double x, double y);
extern double sqrt(double x);
extern float sqrtf(float x);
extern double fabs(double x);
extern float fabsf(float x);
extern double sin(double x);
extern double cos(double x);

double frexp(double x, int *exp);

#define HUGE_VAL (__builtin_huge_val())

/* Unimplemented, but stubbed */
extern double acos(double x);
extern double asin(double x);
extern double atan2(double y, double x);
extern double ceil(double x);
extern double cosh(double x);
extern double ldexp(double a, int exp);
extern double log(double x);
extern double log10(double x);
extern double log2(double x);
extern double sinh(double x);
extern double tan(double x);
extern double tanh(double x);
extern double atan(double x);
extern double log1p(double x);
extern double expm1(double x);

extern double modf(double x, double *iptr);

extern double hypot(double x, double y);

extern double trunc(double x);
extern double acosh(double x);
extern double asinh(double x);
extern double atanh(double x);
extern double erf(double x);
extern double erfc(double x);
extern double gamma(double x);
extern double tgamma(double x);
extern double lgamma(double x);
extern double copysign(double x, double y);
extern double remainder(double x, double y);

enum {
    FP_NAN, FP_INFINITE, FP_ZERO, FP_SUBNORMAL, FP_NORMAL
};

extern int fpclassify(double x);

#define isfinite(x) ((fpclassify(x) != FP_NAN && fpclassify(x) != FP_INFINITE))
#define isnormal(x) (fpclassify(x) == FP_NORMAL)
#define isnan(x)    (fpclassify(x) == FP_NAN)
#define isinf(x)    (fpclassify(x) == FP_INFINITE)

extern float ceilf(float x);
extern double round(double x);
extern float roundf(float x);
extern long lroundf(float x);

_End_C_Header
