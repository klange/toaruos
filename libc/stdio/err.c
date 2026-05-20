#include <err.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

extern char ** __argv;

static inline void __warnerr(int do_exit, int do_perror, int s, const char *f, va_list args) {
	int orig_error = errno;
	fprintf(stderr, "%s: ", __argv[0]);
	if (f) vfprintf(stderr, f, args);
	if (do_perror) fprintf(stderr, "%s%s", f ? ": " : "", strerror(orig_error));
	fprintf(stderr, "\n");
	if (do_exit) exit(s);
}

void vwarn(const char *f, va_list args) { __warnerr(0, 1, 0, f, args); }
void vwarnx(const char *f, va_list args) { __warnerr(0, 0, 0, f, args); }
void verr(int s, const char *f, va_list args) { __warnerr(1, 1, 0, f, args); __builtin_unreachable(); }
void verrx(int s, const char *f, va_list args) { __warnerr(1, 0, 0, f, args); __builtin_unreachable(); }

#define _wrap(do_exit, do_perror) do { \
	va_list args; \
	va_start(args, f); \
	__warnerr(do_exit, do_perror, s,f,args); \
	va_end(args); \
} while (0)

void warn(const char *f, ...)  { int s = 0; _wrap(0, 1); }
void warnx(const char *f, ...) { int s = 0; _wrap(0, 0); }
void err(int s, const char *f, ...)  { _wrap(1, 1); __builtin_unreachable(); }
void errx(int s, const char *f, ...) { _wrap(1, 0); __builtin_unreachable(); }
