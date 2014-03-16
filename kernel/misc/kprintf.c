/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Kernel printf implementation
 *
 * Simple, painfully lacking, implementation of printf(),
 * for the kernel of all things.
 */
#include <system.h>
#include <process.h>
#include <va_list.h>
#include <fs.h>

/*
 * Integer to string
 */
static void print_dec(unsigned int value, unsigned int width, char * buf, int * ptr ) {
	unsigned int n_width = 1;
	unsigned int i = 9;
	while (value > i && i < UINT32_MAX) {
		n_width += 1;
		i *= 10;
		i += 9;
	}

	int printed = 0;
	while (n_width + printed < width) {
		buf[*ptr] = '0';
		*ptr += 1;
		printed += 1;
	}

	i = n_width;
	while (i > 0) {
		unsigned int n = value / 10;
		int r = value % 10;
		buf[*ptr + i - 1] = r + '0';
		i--;
		value = n;
	}
	*ptr += n_width;
}

/*
 * Hexadecimal to string
 */
static void print_hex(unsigned int value, unsigned int width, char * buf, int * ptr) {
	int i = width;

	if (i == 0) i = 8;

	unsigned int n_width = 1;
	unsigned int j = 0x0F;
	while (value > j && j < UINT32_MAX) {
		n_width += 1;
		j *= 0x10;
		j += 0x0F;
	}

	while (i > (int)n_width) {
		buf[*ptr] = '0';
		*ptr += 1;
		i--;
	}

	i = (int)n_width;
	while (i-- > 0) {
		buf[*ptr] = "0123456789abcdef"[(value>>(i*4))&0xF];
		*ptr += + 1;
	}
}

/*
 * vasprintf()
 */
size_t
vasprintf(char * buf, const char *fmt, va_list args) {
	int i = 0;
	char *s;
	int ptr = 0;
	int len = strlen(fmt);
	for ( ; i < len && fmt[i]; ++i) {
		if (fmt[i] != '%') {
			buf[ptr++] = fmt[i];
			continue;
		}
		++i;
		unsigned int arg_width = 0;
		while (fmt[i] >= '0' && fmt[i] <= '9') {
			arg_width *= 10;
			arg_width += fmt[i] - '0';
			++i;
		}
		/* fmt[i] == '%' */
		switch (fmt[i]) {
			case 's': /* String pointer -> String */
				s = (char *)va_arg(args, char *);
				while (*s) {
					buf[ptr++] = *s++;
				}
				break;
			case 'c': /* Single character */
				buf[ptr++] = (char)va_arg(args, int);
				break;
			case 'x': /* Hexadecimal number */
				print_hex((unsigned long)va_arg(args, unsigned long), arg_width, buf, &ptr);
				break;
			case 'd': /* Decimal number */
				print_dec((unsigned long)va_arg(args, unsigned long), arg_width, buf, &ptr);
				break;
			case '%': /* Escape */
				buf[ptr++] = '%';
				break;
			default: /* Nothing at all, just dump it */
				buf[ptr++] = fmt[i];
				break;
		}
	}
	/* Ensure the buffer ends in a null */
	buf[ptr] = '\0';
	return ptr;

}

short  kprint_to_screen = 0;
void * kprint_to_file   = NULL;

unsigned short * textmemptr = (unsigned short *)0xB8000;
void placech(unsigned char c, int x, int y, int attr) {
	unsigned short *where;
	unsigned att = attr << 8;
	where = textmemptr + (y * 80 + x);
	*where = c | att;
}

int x = 0, y = 0;
int _not_ready = 1;
int _off = 0;

/**
 * (Kernel) Print a formatted string.
 * %s, %c, %x, %d, %%
 *
 * @param fmt Formatted string to print
 * @param ... Additional arguments to format
 */
int
kprintf(
		const char *fmt,
		...
	   ) {
	char buf[1024] = {-1};
	va_list args;
	va_start(args, fmt);
	int out = vasprintf(buf, fmt, args);
	/* We're done with our arguments */
	va_end(args);
	if (kprint_to_screen) {
		/*
		 * VGA output for debugging. This is not nearly as feature-complete as the old
		 * kernel terminal; it is here to provide acceptable parsing of the debug messages
		 * and print them to the screen while in early boot on real hardware without a
		 * serial line and should not be used otherwise.
		 */
		if (_not_ready) {
			int temp = 0xFFFF;
			outportb(0x3D4, 14);
			outportb(0x3D5, temp >> 8);
			outportb(0x3d4, 15);
			outportb(0x3d5, temp);
			for (int y = 0; y < 80; ++y) {
				for (int x = 0; x < 24; ++x) {
					placech(' ', x, y, 0x00);
				}
			}
			_not_ready = 0;
		}
		unsigned char *c = (uint8_t *)buf;
		while (*c) {
			if (_off) {
				if (*c > 'a' && *c < 'z') {
					_off = 0;
				} else {
					c++;
					continue;
				}
			} else if (*c == '\033') {
				c++;
				if (*c == '[') {
					_off = 1;
				}
			} else if (*c == '\n') {
				y += 1;
				x = 0;
			} else {
				placech(*c, x, y, 0x07);
				x++;
			}
			if (x == 80) {
				x = 0;
				y++;
			}
			if (y == 24) {
				y = 0;
				x = 0;
			}
			c++;
		}
	}
	/* Registered output file */
	if (kprint_to_file) {
		fs_node_t * node = (fs_node_t *)kprint_to_file;
		uint32_t out = write_fs(node, node->offset, strlen(buf), (uint8_t *)buf);
		node->offset += out;
	}
	return out;
}

int
sprintf(
		char * buf,
		const char *fmt,
		...
	   ) {
	va_list args;
	va_start(args, fmt);
	int out = vasprintf(buf, fmt, args);
	va_end(args);
	return out;
}

