#include <stdio.h>
#include <ctype.h>
#include <string.h>

extern char * _argv_0;
extern int __libc_debug;

int vsscanf(const char *str, const char *format, va_list ap) {
	if (__libc_debug) fprintf(stderr, "%s: sscanf(\"%s\", format=\"%s\", ...);\n", _argv_0, str, format);
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
			int _long = 0;

			if (*format == 'l') {
				format++;
				if (*format == 'l') {
					_long = 1;
					if (__libc_debug) fprintf(stderr, "%s: \033[33;3mWarning\033[0m: Need to scan a large pointer (%d)\n", _argv_0, _long);
					format++;
				}
			}

			if (*format == 'd') {
				int i = 0;
				int sign = 1;
				while (isspace(*str)) str++;
				if (*str == '-') {
					sign = -1;
					str++;
				}
				while (*str && *str >= '0' && *str <= '9') {
					i = i * 10 + *str - '0';
					str++;
				}
				int * out = (int *)va_arg(ap, int*);
				if (__libc_debug) fprintf(stderr, "%s: sscanf: out %d\n", _argv_0, i);
				count++;
				*out = i * sign;
			} else if (*format == 'u') {
				unsigned int i = 0;
				while (isspace(*str)) str++;
				while (*str && *str >= '0' && *str <= '9') {
					i = i * 10 + *str - '0';
					str++;
				}
				unsigned int * out = (unsigned int *)va_arg(ap, unsigned int*);
				if (__libc_debug) fprintf(stderr, "%s: sscanf: out %d\n", _argv_0, i);
				count++;
				*out = i;
			}
		} else {
			/* Expect exact character? */
			if (*str == *format) {
				str++;
			} else {
				break;
			}
		}
		format++;
	}
	return count;
}

int vfscanf(FILE * stream, const char *format, va_list ap) {
	if (__libc_debug) fprintf(stderr, "%s: fscanf(%d, format=%s, ...);\n", _argv_0, fileno(stream), format);
	int count = 0;
	while (*format) {
		if (*format == ' ') {
			/* Handle whitespace */
			int c = fgetc(stream);
			do {
				if (c == EOF) break;
				if (!isspace(c)) {
					ungetc(c, stream);
					break;
				}
				c = fgetc(stream);
			} while (1);
		} else if (*format == '%') {
			/* Parse */
			format++;

			int _fieldwidth = 0;
			while (*format >= '0' && *format <= '9') {
				_fieldwidth *= 10;
				_fieldwidth += *format++;
			}

			if (*format == 's') {
				char * out = (char *)va_arg(ap, char*);
				ssize_t r = 0;

				do {
					int c = fgetc(stream);
					if (c == EOF) break;
					if (c == '\0') break;

					*out++ = c;
					*out = '\0';

					if (_fieldwidth && r == _fieldwidth) break;
				} while (1);

				count += 1;
			}

		} else {
			/* Expect exact character? */
		}
		format++;
	}
	return count;
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
