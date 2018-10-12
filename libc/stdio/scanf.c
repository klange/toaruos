#include <stdio.h>
#include <ctype.h>
#include <string.h>

extern char * _argv_0;

int vsscanf(const char *str, const char *format, va_list ap) {
	fprintf(stderr, "%s: sscanf(\"%s\", format=\"%s\", ...);\n", _argv_0, str, format);
	int count = 0;
	while (*format) {
		if (*format == ' ') {
			/* handle white space */
			while (*str && isspace(*str)) {
				str++;
			}
		} else if (*format == '%') {
			/* Parse */
			format++;

			if (*format == 'd') {
				int i = 0;
				while (*str && *str > '0' && *str < '9') {
					i = i * 10 + *str - '0';
					str++;
				}
				int * out = (int *)va_arg(ap, int*);
				fprintf(stderr, "%s: sscanf: out %d\n", _argv_0, i);
				count++;
				*out = i;
			}
		} else {
			/* Expect exact character? */
			if (*str == *format) {
				str++;
			}
		}
		format++;
	}
	return count;
}

int vfscanf(FILE * stream, const char *format, va_list ap) {
	fprintf(stderr, "%s: fscanf(%d, format=%s, ...);\n", _argv_0, fileno(stream), format);
	while (*format) {
		if (*format == ' ') {
			/* Handle whitespace */
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

int fscanf(FILE *stream, const char *format, ...) {
	va_list args;
	va_start(args, format);
	int out = vfscanf(stream, format, args);
	va_end(args);
	return out;
}

int scanf(const char *format, ...) {
	va_list args;
	va_start(args, format);
	int out = vfscanf(stdin, format, args);
	va_end(args);
	return out;
}
