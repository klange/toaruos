#include <stdio.h>
#include <ctype.h>
#include <string.h>

extern char * _argv_0;

int vsscanf(const char *str, const char *format, va_list ap) {
	fprintf(stderr, "%s: sscanf(..., format=%s, ...);\n", _argv_0, format);
	while (*format) {
		if (*format == ' ') {
			/* handle white space */
			while (*str && isspace(*str)) {
				str++;
			}
		} else if (*format == '%') {
			/* Parse */
		} else {
			/* Expect exact character? */
		}
		format++;
	}
	return 0;
}

int sscanf(const char *str, const char *format, ...) {
	va_list args;
	va_start(args, format);
	int out = vsscanf(str, format, args);
	va_end(args);
	return out;
}
