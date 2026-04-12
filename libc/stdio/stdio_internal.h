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
};

/* Flag values */
#define STDIO_EOF         0x0001
#define STDIO_ERROR       0x0002
#define STDIO_UNSEEKABLE  0x0004


