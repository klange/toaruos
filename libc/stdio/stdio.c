#include <stdio.h>
#include <syscall.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

#include <_xlog.h>

struct _FILE {
	int fd;

	char * read_buf;
	int available;
	int offset;
	int read_from;
	int ungetc;
	int eof;
};

FILE _stdin = {
	.fd = 0,
	.read_buf = NULL,
	.available = 0,
	.offset = 0,
	.read_from = 0,
	.ungetc = -1,
	.eof = 0,
};

FILE _stdout = {
	.fd = 1,
	.read_buf = NULL,
	.available = 0,
	.offset = 0,
	.read_from = 0,
	.ungetc = -1,
	.eof = 0,
};

FILE _stderr = {
	.fd = 2,
	.read_buf = NULL,
	.available = 0,
	.offset = 0,
	.read_from = 0,
	.ungetc = -1,
	.eof = 0,
};

FILE * stdin = &_stdin;
FILE * stdout = &_stdout;
FILE * stderr = &_stderr;

void __stdio_init_buffers(void) {
	_stdin.read_buf = malloc(BUFSIZ);
	//_stdout.read_buf = malloc(BUFSIZ);
	//_stderr.read_buf = malloc(BUFSIZ);
}

#if 0
static char * stream_id(FILE * stream) {
	static char out[] = "stream\0\0\0\0\0\0";
	if (stream == &_stdin) return "stdin";
	if (stream == &_stdout) return "stdout";
	if (stream == &_stderr) return "stderr";
	sprintf(out, "stream %d", fileno(stream));
	return out;
}
#endif

extern char * _argv_0;


static size_t read_bytes(FILE * f, char * out, size_t len) {
	size_t r_out = 0;

	//fprintf(stderr, "%s: Read %d bytes from %s\n", _argv_0, len, stream_id(f));
	//fprintf(stderr, "%s: off[%d] avail[%d] read[%d]\n", _argv_0, f->offset, f->available, f->read_from);

	while (len > 0) {
		if (f->ungetc >= 0) {
			*out = f->ungetc;
			len--;
			out++;
			r_out++;
			f->ungetc = -1;
			continue;
		}

		if (f->available == 0) {
			if (f->offset == BUFSIZ) {
				f->offset = 0;
			}
			ssize_t r = read(fileno(f), &f->read_buf[f->offset], BUFSIZ - f->offset);
			if (r < 0) {
				//fprintf(stderr, "error condition\n");
				return r_out;
			} else {
				f->read_from = f->offset;
				f->available = r;
				f->offset += f->available;
			}
		}

		if (f->available == 0) {
			/* EOF condition */
			//fprintf(stderr, "%s: no bytes available, returning read value of %d\n", _argv_0, r_out);
			f->eof = 1;
			return r_out;
		}

		//fprintf(stderr, "%s: reading until %d reaches %d or %d reaches 0\n", _argv_0, f->read_from, f->offset, len);
		while (f->read_from < f->offset && len > 0) {
			*out = f->read_buf[f->read_from];
			len--;
			f->read_from++;
			f->available--;
			out++;
			r_out += 1;
		}
	}

	//fprintf(stderr, "%s: read completed, returning read value of %d\n", _argv_0, r_out);
	return r_out;
}

FILE * fopen(const char *path, const char *mode) {

	const char * x = mode;

	int flags = 0;
	int mask = 0;

	while (*x) {
		if (*x == 'a') {
			flags |= O_WRONLY;
			flags |= O_APPEND;
			flags |= O_CREAT;
		}
		if (*x == 'w') {
			flags |= O_WRONLY;
		}
		if (*x == '+') {
			if (flags & O_WRONLY) {
				flags |= O_CREAT;
				mask = 0666;
			} else {
				flags |= O_WRONLY;
			}
		}
		++x;
	}

	int fd = syscall_open(path, flags, mask);

	if (fd < 0) {
		return NULL;
	}

	FILE * out = malloc(sizeof(FILE));
	out->fd = fd;
	out->read_buf = malloc(BUFSIZ);
	out->available = 0;
	out->read_from = 0;
	out->offset = 0;
	out->ungetc = -1;
	out->eof = 0;

	return out;
}

int ungetc(int c, FILE * stream) {
	if (stream->ungetc > 0)
		return EOF;

	return (stream->ungetc = c);
}

FILE * fdopen(int fd, const char *mode){
	FILE * out = malloc(sizeof(FILE));
	out->fd = fd;
	out->read_buf = malloc(BUFSIZ);
	out->available = 0;
	out->read_from = 0;
	out->offset = 0;
	out->ungetc = -1;
	out->eof = 0;

	return out;
}

int fclose(FILE * stream) {
	int out = syscall_close(stream->fd);
	free(stream->read_buf);
	if (stream == &_stdin || stream == &_stdout || stream == &_stderr) {
		return out;
	} else {
		free(stream);
		return out;
	}
}

int fseek(FILE * stream, long offset, int whence) {
	//fprintf(stderr, "%s: seek called, resetting\n", _argv_0);
	stream->offset = 0;
	stream->read_from = 0;
	stream->available = 0;
	stream->ungetc = -1;
	stream->eof = 0;

	int resp = syscall_lseek(stream->fd,offset,whence);
	if (resp < 0) {
		return -1;
	}
	return 0;
}

long ftell(FILE * stream) {
	//fprintf(stderr, "%s: tell called, resetting\n", _argv_0);
	stream->offset = 0;
	stream->read_from = 0;
	stream->available = 0;
	stream->ungetc = -1;
	stream->eof = 0;
	return syscall_lseek(stream->fd, 0, SEEK_CUR);
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE * stream) {
	char * tracking = (char*)ptr;
	for (size_t i = 0; i < nmemb; ++i) {
		int r = read_bytes(stream, tracking, size);
		if (r < 0) {
			return -1;
		}
		tracking += r;
		if (r < (int)size) {
			return i;
		}
	}
	return nmemb;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE * stream) {
	char * tracking = (char*)ptr;
	for (size_t i = 0; i < nmemb; ++i) {
		int r = syscall_write(stream->fd, tracking, size);
		if (r < 0) {
			_XLOG("write error in fwrite");
			return -1;
		}
		tracking += r;
		if (r < (int)size) {
			return i;
		}
	}
	return nmemb;
}

int fileno(FILE * stream) {
	return stream->fd;
}

int fflush(FILE * stream) {
	return 0;
}

int fputs(const char *s, FILE *stream) {
	fwrite(s, strlen(s), 1, stream);
	/* eof? */
	return 0;
}

int fputc(int c, FILE *stream) {
	char data[] = {c};
	fwrite(data, 1, 1, stream);
	return c;
}

int fgetc(FILE * stream) {
	char buf[1];
	int r;
	r = fread(buf, 1, 1, stream);
	if (r < 0) {
		stream->eof = 1;
		return EOF;
	} else if (r == 0) {
		stream->eof = 1;
		return EOF;
	}
	return buf[0];
}

char *fgets(char *s, int size, FILE *stream) {
	int c;
	char * out = s;
	while ((c = fgetc(stream)) > 0) {
		*s++ = c;
		size--;
		if (size == 0) {
			return out;
		}
		*s = '\0';
		if (c == '\n') {
			return out;
		}
	}
	if (c == EOF) {
		stream->eof = 1;
		if (out == s) {
			return NULL;
		} else {
			return out;
		}
	}
	return NULL;
}

int putchar(int c) {
	return fputc(c, stdout);
}

void rewind(FILE *stream) {
	fseek(stream, 0, SEEK_SET);
}

void setbuf(FILE * stream, char * buf) {
	// ...
}

int feof(FILE * stream) {
	return stream->eof;
}
