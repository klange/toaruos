#pragma once

#include <stddef.h>
#include <va_list.h>

typedef struct _FILE FILE;

#define BUFSIZ 1024

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

extern size_t fread(void *ptr, size_t size, size_t nmemb, FILE * stream);
extern size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE * stream);

extern int fileno(FILE * stream);
extern int fflush(FILE * stream);

extern size_t vasprintf(char * buf, const char *fmt, va_list args);
extern int    sprintf(char *buf, const char *fmt, ...);
extern int    fprintf(FILE *stream, char *fmt, ...);
extern int    printf(char *fmt, ...);

extern int puts(const char *s);
extern int fputs(const char *s, FILE *stream);
extern int fputc(int c, FILE *stream);
extern int fgetc(FILE *stream);
extern char *fgets(char *s, int size, FILE *stream);

extern void rewind(FILE *stream);
extern void setbuf(FILE * stream, char * buf);

extern void perror(const char *s);

extern int ungetc(int c, FILE * stream);

extern int feof(FILE * stream);
