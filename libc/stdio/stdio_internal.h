#pragma once
#include <va_list.h>

struct _FILE {
	int fd;

	char * read_buf;
	int available;
	int offset;
	int read_from;
	int ungetc;
	int bufsiz;
	long last_read_start;
	char * _name;

	char * write_buf;
	size_t written;
	size_t wbufsiz;

	struct _FILE * prev;
	struct _FILE * next;

	int flags;

	pid_t popen_pid;
	int bufflags;
};

/* Flag values */
#define STDIO_EOF         0x0001
#define STDIO_ERROR       0x0002
#define STDIO_UNSEEKABLE  0x0004

#define STDIO_BUF_READ_FREE  1
#define STDIO_BUF_WRITE_FREE 2

extern void __stdio_init_buffers(void);
extern void __stdio_cleanup(void);
extern size_t __printf_internal(int (*callback)(void *, char), void * userData, const char * fmt, va_list args);

