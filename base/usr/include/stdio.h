#pragma once

#include <stddef.h>
#include <va_list.h>

typedef struct _FILE FILE;

#define BUFSIZ 8192

extern FILE * stdin;
extern FILE * stdout;
extern FILE * stderr;

#define EOF (-1)

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

extern FILE * fopen(const char *path, const char *mode);
extern int fclose(FILE * stream);
extern int fseek(FILE * stream, long offset, int whence);
extern long ftell(FILE * stream);
extern FILE * fdopen(int fd, const char *mode);
extern FILE * freopen(const char *path, const char *mode, FILE * stream);

extern size_t fread(void *ptr, size_t size, size_t nmemb, FILE * stream);
extern size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE * stream);

extern int fileno(FILE * stream);
extern int fflush(FILE * stream);

extern int vasprintf(char ** buf, const char *fmt, va_list args);
extern int sprintf(char *buf, const char *fmt, ...);
extern int fprintf(FILE *stream, const char *fmt, ...);
extern int printf(const char *fmt, ...);
extern int snprintf(char * buf, size_t size, const char * fmt, ...);
extern int vsprintf(char * buf, const char *fmt, va_list args);
extern int vsnprintf(char * buf, size_t size, const char *fmt, va_list args);
extern int vfprintf(FILE * device, const char *format, va_list ap);

extern int puts(const char *s);
extern int fputs(const char *s, FILE *stream);
extern int fputc(int c, FILE *stream);
#define putc(c,s) fputc((c),(s))
#define putchar(c) fputc((c),stdout)
extern int fgetc(FILE *stream);
extern char *fgets(char *s, int size, FILE *stream);

extern void rewind(FILE *stream);
extern void setbuf(FILE * stream, char * buf);

extern void perror(const char *s);

extern int ungetc(int c, FILE * stream);

extern int feof(FILE * stream);
extern void clearerr(FILE * stream);
extern int ferror(FILE * stream);

extern char * strerror(int errnum);

extern int _fwouldblock(FILE * stream);

extern FILE * tmpfile(void);

extern int setvbuf(FILE * stream, char * buf, int mode, size_t size);

extern int remove(const char * pathname);
extern int rename(const char * oldpath, const char * newpath);

#define _IONBF 0
#define _IOLBF 1
#define _IOFBF 2

#define getc(s) fgetc(s)

extern char * tmpnam(char * s);
#define L_tmpnam 256
