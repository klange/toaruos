#include <stdio.h>
#include <syscall.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

struct _FILE {
	int fd;

	char * read_buf;
	int available;
	int offset;
	int read_from;
	int ungetc;
	int eof;
	int bufsiz;
	long last_read_start;
	char * _name;

	char * write_buf;
	size_t written;
	size_t wbufsiz;

	struct _FILE * prev;
	struct _FILE * next;
};

FILE _stdin = {
	.fd = 0,
	.read_buf = NULL,
	.available = 0,
	.offset = 0,
	.read_from = 0,
	.ungetc = -1,
	.eof = 0,
	.last_read_start = 0,
	.bufsiz = BUFSIZ,

	.wbufsiz = BUFSIZ,
	.write_buf = NULL,
	.written = 0,
};

FILE _stdout = {
	.fd = 1,
	.read_buf = NULL,
	.available = 0,
	.offset = 0,
	.read_from = 0,
	.ungetc = -1,
	.eof = 0,
	.last_read_start = 0,
	.bufsiz = BUFSIZ,

	.wbufsiz = BUFSIZ,
	.write_buf = NULL,
	.written = 0,
};

FILE _stderr = {
	.fd = 2,
	.read_buf = NULL,
	.available = 0,
	.offset = 0,
	.read_from = 0,
	.ungetc = -1,
	.eof = 0,
	.last_read_start = 0,
	.bufsiz = BUFSIZ,

	.wbufsiz = BUFSIZ,
	.write_buf = NULL,
	.written = 0,
};

FILE * stdin = &_stdin;
FILE * stdout = &_stdout;
FILE * stderr = &_stderr;

static FILE * _head = NULL;

void __stdio_init_buffers(void) {
	_stdin.read_buf = malloc(BUFSIZ);
	_stdout.write_buf = malloc(BUFSIZ);
	_stderr.write_buf = malloc(BUFSIZ);
	_stdin._name = strdup("stdin");
	_stdout._name = strdup("stdout");
	_stderr._name = strdup("stderr");
}

void __stdio_cleanup(void) {
	if (stdout) fflush(stdout);
	if (stderr) fflush(stderr);
	while (_head) {
		fclose(_head);
	}
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

extern int __libc_debug;
extern char * _argv_0;

int setvbuf(FILE * stream, char * buf, int mode, size_t size) {
	if (mode != _IOLBF) {
		return -1; /* Unsupported */
	}
	if (buf) {
		if (stream->read_buf) {
			free(stream->read_buf);
		}
		stream->read_buf = buf;
		stream->bufsiz = size;
	}
	return 0;
}

int fflush(FILE * stream) {
	if (!stream->write_buf) return EOF;
	if (stream->written) {
		syscall_write(stream->fd, stream->write_buf, stream->written);
		stream->written = 0;
	}
	return 0;
}

static size_t write_bytes(FILE * f, char * buf, size_t len) {
	if (!f->write_buf) return 0;

	size_t newBytes = 0;
	while (len > 0) {
		f->write_buf[f->written++] = *buf;
		if (f->written == (size_t)f->wbufsiz || *buf == '\n') {
			fflush(f);
		}
		newBytes++;
		buf++;
		len--;
	}

	return newBytes;
}

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
			if (f->offset == f->bufsiz) {
				f->offset = 0;
			}
			f->last_read_start = syscall_seek(f->fd, 0, SEEK_CUR);
			ssize_t r = read(fileno(f), &f->read_buf[f->offset], f->bufsiz - f->offset);
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
		while (f->read_from < f->offset && len > 0 && f->available > 0) {
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

static void parse_mode(const char * mode, int * flags_, int * mask_) {
	const char * x = mode;

	int flags = 0;
	int mask = 0644;

	while (*x) {
		if (*x == 'a') {
			flags |= O_WRONLY;
			flags |= O_APPEND;
			flags |= O_CREAT;
		}
		if (*x == 'w') {
			flags |= O_WRONLY;
			flags |= O_CREAT;
			flags |= O_TRUNC;
			mask = 0666;
		}
		if (*x == '+') {
			flags |= O_RDWR;
			flags &= ~(O_APPEND); /* uh... */
		}
		++x;
	}

	*flags_  = flags;
	*mask_ = mask;
}


FILE * fopen(const char *path, const char *mode) {

	int flags, mask;
	parse_mode(mode, &flags, &mask);
	int fd = syscall_open(path, flags, mask);

	if (fd < 0) {
		errno = -fd;
		return NULL;
	}

	FILE * out = malloc(sizeof(FILE));
	memset(out, 0, sizeof(struct _FILE));
	out->fd = fd;
	out->read_buf = malloc(BUFSIZ);
	out->bufsiz = BUFSIZ;
	out->available = 0;
	out->read_from = 0;
	out->offset = 0;
	out->ungetc = -1;
	out->eof = 0;
	out->_name = strdup(path);

	out->write_buf = malloc(BUFSIZ);
	out->written = 0;
	out->wbufsiz = BUFSIZ;

	out->next = _head;
	if (_head) _head->prev = out;
	_head = out;

	return out;
}

/* This is very wrong */
FILE * freopen(const char *path, const char *mode, FILE * stream) {

	if (path) {
		fflush(stream);
		syscall_close(stream->fd);
		int flags, mask;
		parse_mode(mode, &flags, &mask);
		int fd = syscall_open(path, flags, mask);
		stream->fd = fd;
		stream->available = 0;
		stream->read_from = 0;
		stream->offset = 0;
		stream->ungetc = -1;
		stream->eof = 0;
		stream->_name = strdup(path);
		stream->written = 0;
		if (stream != &_stdin && stream != &_stdout && stream != &_stderr) {
			stream->next = _head;
			if (_head) _head->prev = stream;
			_head = stream;
		}
		if (fd < 0) {
			errno = -fd;
			return NULL;
		}
	}

	return stream;
}

int ungetc(int c, FILE * stream) {
	if (stream->ungetc > 0)
		return EOF;

	return (stream->ungetc = c);
}

FILE * fdopen(int fd, const char *mode){
	FILE * out = malloc(sizeof(FILE));
	memset(out, 0, sizeof(struct _FILE));
	out->fd = fd;
	out->read_buf = malloc(BUFSIZ);
	out->bufsiz = BUFSIZ;
	out->available = 0;
	out->read_from = 0;
	out->offset = 0;
	out->ungetc = -1;
	out->eof = 0;

	char tmp[30];
	sprintf(tmp, "fd[%d]", fd);
	out->_name = strdup(tmp);

	out->write_buf = malloc(BUFSIZ);
	out->written = 0;
	out->wbufsiz = BUFSIZ;

	out->next = _head;
	if (_head) _head->prev = out;
	_head = out;

	return out;
}

int _fwouldblock(FILE * stream) {
	return !stream->available;
}

int fclose(FILE * stream) {
	fflush(stream);
	int out = syscall_close(stream->fd);
	free(stream->_name);
	free(stream->read_buf);
	if (stream->write_buf) free(stream->write_buf);
	stream->write_buf = NULL;
	if (stream == &_stdin || stream == &_stdout || stream == &_stderr) {
		return out;
	} else {

		if (stream->prev) stream->prev->next = stream->next;
		if (stream->next) stream->next->prev = stream->prev;
		if (stream == _head) _head = stream->next;

		free(stream);
		return out;
	}
}

static char * _whence_str(int whence) {
	if (whence == SEEK_SET) return "SEEK_SET";
	if (whence == SEEK_CUR) return "SEEK_CUR";
	if (whence == SEEK_END) return "SEEK_END";
	return "?";
}

int fseek(FILE * stream, long offset, int whence) {
	if (stream->read_from && whence == SEEK_CUR) {
		if (_argv_0 && strcmp(_argv_0, "ld.so")) {
			if (__libc_debug) {
				fprintf(stderr, "%s: fseek(%s, %ld, %s)\n", _argv_0, stream->_name, offset, _whence_str(whence));
				fprintf(stderr, "\033[33;3mWARNING\033[0m: seeking when offset is currently %d\n", stream->read_from);
				fprintf(stderr, "\033[33;3mWARNING\033[0m: this may not be reflected in kernel\n");
			}
		}
		offset = offset + stream->read_from + stream->last_read_start;
		whence = SEEK_SET;
	}
	if (stream->written) {
		fflush(stream);
	}
	stream->offset = 0;
	stream->read_from = 0;
	stream->available = 0;
	stream->ungetc = -1;
	stream->eof = 0;

	int resp = syscall_seek(stream->fd,offset,whence);
	if (resp < 0) {
		errno = -resp;
		return -1;
	}
	return 0;
}

long ftell(FILE * stream) {
	if (_argv_0 && strcmp(_argv_0, "ld.so") && __libc_debug) {
		fprintf(stderr, "%s: ftell(%s)\n", _argv_0, stream->_name);
	}
	if (stream->written) {
		fflush(stream);
	}
	if (stream->read_from || stream->last_read_start) {
		return stream->last_read_start + stream->read_from;
	}
	stream->offset = 0;
	stream->read_from = 0;
	stream->available = 0;
	stream->ungetc = -1;
	stream->eof = 0;
	long resp = syscall_seek(stream->fd, 0, SEEK_CUR);
	if (resp < 0) {
		errno = -resp;
		return -1;
	}
	return resp;
}

int fgetpos(FILE *stream, fpos_t *pos) {
	long ret = ftell(stream);
	if (ret == -1) return -1;

	*pos = ret;
	return 0;
}

int fsetpos(FILE *stream, const fpos_t *pos) {
	return fseek(stream, *pos, SEEK_SET);
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
		int r = write_bytes(stream, tracking, size);
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

int fileno(FILE * stream) {
	return stream->fd;
}

int fputs(const char *s, FILE *stream) {
	while (*s) {
		fputc(*s++, stream);
	}
	return 0;
}

int fputc(int c, FILE *stream) {
	char data[] = {c};
	write_bytes(stream, data, 1);
	return c;
}

int putc(int c, FILE *stream) __attribute__((weak, alias("fputc")));

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
	return (unsigned char)buf[0];
}

int getc(FILE * stream) __attribute__((weak, alias("fgetc")));

int getchar(void) {
	return fgetc(stdin);
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

void clearerr(FILE * stream) {
	stream->eof = 0;
}

int ferror(FILE * stream) {
	return 0; /* TODO */
}
