#include <stdlib.h>
#include <math.h>
#include <stdio.h>

double strtod(const char *nptr, char **endptr) {
	int sign = 1;
	if (*nptr == '-') {
		sign = -1;
		nptr++;
	}

	long long decimal_part = 0;

	while (*nptr && *nptr != '.') {
		if (*nptr < '0' || *nptr > '9') {
			break;
		}
		decimal_part *= 10LL;
		decimal_part += (long long)(*nptr - '0');
		nptr++;
	}

	double sub_part = 0;
	double multiplier = 0.1;

	if (*nptr == '.') {
		nptr++;

		while (*nptr) {
			if (*nptr < '0' || *nptr > '9') {
				break;
			}

			sub_part += multiplier * (*nptr - '0');
			multiplier *= 0.1;
			nptr++;
		}
	}

	double expn = (double)sign;

	if (*nptr == 'e' || *nptr == 'E') {
		nptr++;

		int exponent_sign = 1;

		if (*nptr == '+') {
			nptr++;
		} else if (*nptr == '-') {
			exponent_sign = -1;
			nptr++;
		}

		int exponent = 0;

		while (*nptr) {
			if (*nptr < '0' || *nptr > '9') {
				break;
			}
			exponent *= 10;
			exponent += (*nptr - '0');
			nptr++;
		}

		expn = pow(10.0,(double)(exponent * exponent_sign));
	}

	if (endptr) {
		*endptr = (char *)nptr;
	}
	double result = ((double)decimal_part + sub_part) * expn;
	return result;
}

float strtof(const char *nptr, char **endptr) {
	return strtod(nptr,endptr);
}
