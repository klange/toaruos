#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

ssize_t getdelim(char **restrict lineptr, size_t *restrict n, int delimiter, FILE *restrict stream) {
	if (!lineptr || !n) {
		errno = EINVAL;
		/* XXX Are we supposed to set the stream to failed for this case? Seems dubious. */
		return -1;
	}

	size_t c = 0;
	while (1) {
		int i = fgetc(stream);

		if (*n <= c + 1) {
			size_t nn = *n < 120 ? 120 : (*n * 2);
			/* TODO: We don't define SSIZE_MAX? Anyway, ssize_t is typedefed to long on all both of our supported
			 * platforms, so use LONG_MAX here as a replacement. size_t should be able to hold SSIZE_MAX * 2, so
			 * the above calculation should not overflow and we can check here if *n has gotten too big. */
			if (nn > LONG_MAX) nn = LONG_MAX;
			if (nn <= c + 1) {
				/* ... and this check should only trip if we've maxed up *n to SSIZE_MAX
				 *     and it's still not big enought to fit our data. */
				errno = EOVERFLOW;
				return -1;
			}
			char * nlineptr = realloc(*lineptr, nn);
			if (!nlineptr) return -1; /* malloc failure */
			*n = nn;
			*lineptr = nlineptr;
		}

		if (i == EOF) {
			(*lineptr)[c] = '\0';
			if (c) return c;
			return -1;
		}

		(*lineptr)[c++] = i;
		if (i == delimiter) break;
	}

	(*lineptr)[c] = '\0';
	return c;
}

ssize_t getline(char **restrict lineptr, size_t *restrict n, FILE *restrict stream) {
	return getdelim(lineptr, n, '\n', stream);
}


