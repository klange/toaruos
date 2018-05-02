/* Really bad atof */

#include <stdlib.h>

double atof(const char * nptr) {
	int sign = 1;
	if (*nptr == '-') {
		sign = -1;
		nptr++;
	}

	long long decimal_part = 0;

	while (*nptr && *nptr != '.') {
		if (*nptr < '0' || *nptr > '9') {
			return 0.0;
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
				return ((double)decimal_part) * (double)(sign);
			}

			sub_part += multiplier * (*nptr - '0');
			multiplier *= 0.1;
			nptr++;
		}
	}

	return ((double)decimal_part + sub_part) * (double)(sign);
}
