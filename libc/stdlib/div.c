#include <stdlib.h>

div_t div(int numerator, int denominator) {
	div_t out;
	out.quot = numerator / denominator;
	out.rem  = numerator % denominator;

	if (numerator >= 0 && out.rem < 0) {
		out.quot++;
		out.rem -= denominator;
	} else if (numerator < 0 && out.rem > 0) {
		out.quot--;
		out.rem += denominator;
	}

	return out;
}

ldiv_t ldiv(long numerator, long denominator) {
	ldiv_t out;
	out.quot = numerator / denominator;
	out.rem  = numerator % denominator;

	if (numerator >= 0 && out.rem < 0) {
		out.quot++;
		out.rem -= denominator;
	} else if (numerator < 0 && out.rem > 0) {
		out.quot--;
		out.rem += denominator;
	}

	return out;
}
