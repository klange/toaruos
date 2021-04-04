#pragma once

#include <string.h>
#include <stdarg.h>
typedef void * FILE;

extern FILE * stdout;
extern FILE * stderr;

extern int fprintf(FILE *stream, const char * fmt, ...);
extern int snprintf(char * str, size_t size, const char * format, ...);
extern int fputc(int c, FILE * stream);
extern int vsnprintf(char *str, size_t size, const char *format, va_list ap);
extern int puts(const char * s);

#define printf(...) fprintf(stdout, __VA_ARGS__)


