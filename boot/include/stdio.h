#pragma once

#include <string.h>
#include <stdarg.h>
typedef void * FILE;

extern FILE * stdout;
extern FILE * stderr;
extern FILE * stdin;

extern int fprintf(FILE *stream, const char * fmt, ...);
extern int snprintf(char * str, size_t size, const char * format, ...);
extern int fputc(int c, FILE * stream);
extern int vsnprintf(char *str, size_t size, const char *format, va_list ap);
extern int puts(const char * s);

#define printf(...) fprintf(stdout, __VA_ARGS__)

#ifdef EFI_PLATFORM
extern int fgetc(FILE * stream);
extern FILE * fopen(const char * pathname, const char * mode);
extern int fclose(FILE * stream);
extern size_t fread(void * ptr, size_t size, size_t nmemb, FILE * stream);
extern int fseek(FILE * stream, long offset, int whence);
extern long ftell(FILE * stream);
#define SEEK_SET 1
#define SEEK_END 2
struct stat {
    int pad;
};
extern int stat(const char*,struct stat*);
extern int errno;
extern char * strerror(int errnum);
extern int feof(FILE * stream);
#define fflush(o)
#define ferror(o) (0)
#define fwrite(a,b,c,d) (0)
#endif
