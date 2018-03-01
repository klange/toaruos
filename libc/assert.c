#include <stdio.h>

void __assert_func(const char * file, int line, const char * func, const char * failedexpr) {
 fprintf(stderr, "Assertion failed in %s:%d (%s): %s\n", file, line, func, failedexpr);
 // void
}
