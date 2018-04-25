#include <stdio.h>
#include <syscall.h>
#include <string.h>

#include <_xlog.h>

FILE _stdin = {
	.fd = 0,
};

FILE _stdout = {
	.fd = 1,
};

FILE _stderr = {
	.fd = 2,
};

FILE * stdin = &_stdin;
FILE * stdout = &_stdout;
FILE * stderr = &_stderr;

FILE * fopen(const char *path, const char *mode) {

	const char * x = mode;

	int flags = 0;
	int mask = 0;

	while (*x) {
		if (*x == 'a') {
			flags |= 0x08;
			flags |= 0x01;
		}
		if (*x == 'w') {
			flags |= 0x01;
		}
		if (*x == '+') {
			if (flags & 0x01) {
				flags |= 0x200;
				mask = 0666;
			} else {
				flags |= 0x01;
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

	return out;
}

FILE * fdopen(int fd, const char *mode){
	FILE * out = malloc(sizeof(FILE));
	out->fd = fd;

	return out;
}

int fclose(FILE * stream) {
	return syscall_close(stream->fd);
}

int fseek(FILE * stream, long offset, int whence) {
	int resp = syscall_lseek(stream->fd,offset,whence);
	if (resp < 0) {
		return -1;
	}
	return 0;
}

long ftell(FILE * stream) {
	return syscall_lseek(stream->fd, 0, SEEK_CUR);
}


size_t fread(void *ptr, size_t size, size_t nmemb, FILE * stream) {
	char * tracking = (char*)ptr;
	for (size_t i = 0; i < nmemb; ++i) {
		int r = syscall_read(stream->fd, tracking, size);
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
		return EOF;
	} else if (r == 0) {
		return EOF;
	}
	return buf[0];
}

int putchar(int c) {
	return fputc(c, stdout);
}

void rewind(FILE *stream) {
	fseek(stream, 0, SEEK_SET);
}

char *fgets(char *s, int size, FILE *stream) {
	int c;
	char * out = s;
	while ((c = fgetc(stream)) > 0) {
		*s++ = c;
		if (c == '\n') {
			return out;
		}
	}
	if (c == EOF) {
		if (out == s) {
			return NULL;
		} else {
			return out;
		}
	}
	return NULL;
}

void setbuf(FILE * stream, char * buf) {
	// ...
}
