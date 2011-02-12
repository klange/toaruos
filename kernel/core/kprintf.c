#include <system.h>

typedef __builtin_va_list va_list;
#define va_start(ap,last) __builtin_va_start(ap, last)
#define va_end(ap) __builtin_va_end(ap)
#define va_arg(ap,type) __builtin_va_arg(ap,type)
#define va_copy(dest, src) __builtin_va_copy(dest,src)

static char buf[1024] = {-1};
static int ptr = -1;

/*
 * Parse integer
 */
static void
parse_num(
		unsigned int value,
		unsigned int base
		) {
	unsigned int n = value / base;
	int r = value % base;
	if (r < 0) {
		r += base;
		--n;
	}
	if (value >= base) {
		parse_num(n, base);
	}
	buf[ptr++] = (r+'0');
}

/*
 * Parse hexadecimal
 */
static void
parse_hex(
		unsigned int value
		) {
	int i = 8;
	while (i-- > 0) {
		buf[ptr++] = "0123456789abcdef"[(value>>(i*4))&0xF];
	}
}

/*
 * kprintf
 * %s, %c, %x, %d, %%
 * (Kernel) Print a formatted string.
 */
void
kprintf(
		const char *fmt,
		...
	   ) {
	int i = 0;
	char *s;
	va_list args;
	va_start(args, fmt);
	ptr = 0;
	for ( ; fmt[i]; ++i) {
		if ((fmt[i] != '%') && (fmt[i] != '\\')) {
			buf[ptr++] = fmt[i];
			continue;
		} else if (fmt[i] == '\\') {
			switch (fmt[++i]) {
				case 'a': buf[ptr++] = '\a'; break;
				case 'b': buf[ptr++] = '\b'; break;
				case 't': buf[ptr++] = '\t'; break;
				case 'n': buf[ptr++] = '\n'; break;
				case 'r': buf[ptr++] = '\r'; break;
				case '\\':buf[ptr++] = '\\'; break;
			}
			continue;
		}
		/* fmt[i] == '%' */
		switch (fmt[++i]) {
			case 's':
				s = (char *)va_arg(args, char *);
				while (*s) {
					buf[ptr++] = *s++;
				}
				break;
			case 'c':
				buf[ptr++] = (char)va_arg(args, int);
				break;
			case 'x':
				parse_hex((unsigned long)va_arg(args, unsigned long));
				break;
			case 'd':
				parse_num((unsigned long)va_arg(args, unsigned long), 10);
				break;
			case '%':
				buf[ptr++] = '%';
				break;
			default:
				buf[ptr++] = fmt[i];
				break;
		}
	}
	buf[ptr] = '\0';
	va_end(args);
	puts(buf);
}

char * kgets_buffer = NULL;
int kgets_collected = 0;
int kgets_want      = 0;
int kgets_newline   = 0;
int kgets_cancel    = 0;

void
kgets_handler(
		char ch
		) {

	if (ch == 0x08) {
		/* Backspace */
		if (kgets_collected != 0) {
			writech(0x08);
			writech(' ');
			writech(0x08);
			kgets_buffer[kgets_collected] = '\0';
			--kgets_collected;
		}
		return;
	} else if (ch == '\n') {
		kgets_newline = 1;
		writech('\n');
		return;
	} else if (ch < 0x20) {
		writech('^');
		writech(ch + 0x40);
		return;
	} else {
		writech(ch);
	}
	if (kgets_collected < kgets_want) {
		kgets_buffer[kgets_collected] = ch;
		kgets_collected++;
	}
}

int
kgets(
		char *buffer,
		int size
	 ) {
	kgets_buffer    = buffer;
	kgets_collected = 0;
	kgets_want      = size;
	kgets_newline   = 0;
	keyboard_buffer_handler = kgets_handler;
	while ((kgets_collected < size) && (!kgets_newline)) {
		__asm__ __volatile__ ("hlt");
	}
	buffer[kgets_collected] = '\0';
	keyboard_buffer_handler = NULL;
	return kgets_collected;
}
