/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Kernel printf implementation
 *
 * Simple, painfully lacking, implementation of printf(),
 * for the kernel of all things.
 */
#include <system.h>
#include <process.h>

typedef __builtin_va_list va_list;
#define va_start(ap,last) __builtin_va_start(ap, last)
#define va_end(ap) __builtin_va_end(ap)
#define va_arg(ap,type) __builtin_va_arg(ap,type)
#define va_copy(dest, src) __builtin_va_copy(dest,src)

static char buf[1024] = {-1};
static int ptr = -1;

/*
 * Integer to string
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
 * Hexadecimal to string
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

/**
 * (Kernel) Print a formatted string.
 * %s, %c, %x, %d, %%
 *
 * @param fmt Formatted string to print
 * @param ... Additional arguments to format
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
		if (fmt[i] != '%') {
			buf[ptr++] = fmt[i];
			continue;
		}
		/* fmt[i] == '%' */
		switch (fmt[++i]) {
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
				parse_hex((unsigned long)va_arg(args, unsigned long));
				break;
			case 'd': /* Decimal number */
				parse_num((unsigned long)va_arg(args, unsigned long), 10);
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
	/* We're done with our arguments */
	va_end(args);
	/* Print that sucker */
	if (ansi_ready) {
		ansi_print(buf);
	} else {
		puts(buf);
	}
}

int
sprintf(
		char * buf,
		const char *fmt,
		...
	   ) {
	int i = 0;
	char *s;
	va_list args;
	va_start(args, fmt);
	ptr = 0;
	for ( ; fmt[i]; ++i) {
		if (fmt[i] != '%') {
			buf[ptr++] = fmt[i];
			continue;
		}
		/* fmt[i] == '%' */
		switch (fmt[++i]) {
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
				parse_hex((unsigned long)va_arg(args, unsigned long));
				break;
			case 'd': /* Decimal number */
				parse_num((unsigned long)va_arg(args, unsigned long), 10);
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

/*
 * gets() implementation for the kernel
 */

char * kgets_buffer = NULL;
int kgets_collected = 0;
int kgets_want      = 0;
int kgets_newline   = 0;
int kgets_cancel    = 0;
kgets_redraw_t kgets_redraw_func = NULL;
kgets_tab_complete_t kgets_tab_complete_func = NULL;
kgets_special_t kgets_key_down  = NULL;
kgets_special_t kgets_key_up    = NULL;
kgets_special_t kgets_key_left  = NULL;
kgets_special_t kgets_key_right = NULL;
volatile process_t * kgets_client = NULL;

int kgets_offset = 0;

uint8_t kgets_special = 0;

static void
kwrite(
		char ch
		) {
	ansi_put(ch);
	serial_send(ch);
}

/**
 * (Internal) kgets keyboard handler
 */
void
kgets_handler(
		char ch
		) {
	if (kgets_special == 1) {
		if (ch == 91) {
			kgets_special = 2;
			return;
		}
		kgets_special = 0;
		return;
	}
	if (kgets_special == 2) {
		switch (ch) {
			case 66:
				if (kgets_key_down) {
					kgets_key_down(kgets_buffer);
					kgets_collected = strlen(kgets_buffer);
					kgets_offset = kgets_collected;
				}
				break;
			case 65:
				if (kgets_key_up) {
					kgets_key_up(kgets_buffer);
					kgets_collected = strlen(kgets_buffer);
					kgets_offset = kgets_collected;
				}
				break;
			case 68:
				if (kgets_key_left) {
					kgets_key_left(kgets_buffer);
					kgets_collected = strlen(kgets_buffer);
					kgets_offset = kgets_collected;
				} else {
					if (kgets_offset > 0) {
						kwrite(27);
						kwrite(91);
						kwrite(68);
						kgets_offset--;
						bochs_redraw_cursor();
					}
				}
				break;
			case 67:
				if (kgets_key_right) {
					kgets_key_right(kgets_buffer);
					kgets_collected = strlen(kgets_buffer);
					kgets_offset = kgets_collected;
				} else {
					if (kgets_offset < kgets_collected) {
						kwrite(27);
						kwrite(91);
						kwrite(67);
						kgets_offset++;
						bochs_redraw_cursor();
					}
				}
				break;
			default:
				kprintf("Unrecognized: %d\n", ch);
				break;
		}
		kgets_special = 0;
		return;
	}

	if (ch == 0x08) {
		/* Backspace */
		if (kgets_collected != 0) {
			if (kgets_offset == 0) {
				return;
			}			/* Clear the previous character */
			kwrite(0x08);
			kwrite(' ');
			kwrite(0x08);
			if (kgets_offset != kgets_collected) {
				int remaining = kgets_collected - kgets_offset;
				for (int i = 0; i < remaining; ++i) {
					if (current_process != kgets_client) {
						/* Switch page directories into the caller so we can write to its buffers */
						switch_page_directory(kgets_client->thread.page_directory);
					}
					kwrite(kgets_buffer[kgets_offset + i]);
					kgets_buffer[kgets_offset + i - 1] = kgets_buffer[kgets_offset + i];
				}
				kwrite(' ');
				for (int i = 0; i < remaining + 1; ++i) {
					kwrite(27);
					kwrite(91);
					kwrite(68);
				}
				kgets_offset--;
				kgets_collected--;
				bochs_redraw_cursor();
			} else {
				/* Erase the end of the buffer */
				if (current_process != kgets_client) {
					/* Switch page directories into the caller so we can write to its buffers */
					switch_page_directory(kgets_client->thread.page_directory);
				}
				kgets_buffer[--kgets_collected] = '\0';
				kgets_offset--;
			}
		}
		return;
	} else if (ch == '\x0c') {
		kprintf("\033[H\033[2J");
		if (kgets_redraw_func) {
			kgets_redraw_func();
		}
		kgets_redraw_buffer();
		return;
	} else if (ch == '\t' && kgets_tab_complete_func) {
		if (current_process != kgets_client) {
			/* Switch page directories into the caller so we can write to its buffers */
			switch_page_directory(kgets_client->thread.page_directory);
		}
		kgets_tab_complete_func(kgets_buffer);
		kgets_collected = strlen(kgets_buffer);
		return;
	} else if (ch == 27) {
		kgets_special = 1;
		return;
	} else if (ch == '\n') {
		/* Newline finishes off the kgets() */
		while (kgets_offset < kgets_collected) {
			kwrite(27);
			kwrite(91);
			kwrite(67);
			kgets_offset++;
		}
		kwrite('\n');
		kgets_newline = 1;
		return;
	}	/* Add this character to the buffer. */
	if (kgets_offset != kgets_collected) {
		IRQ_OFF;
		if (current_process != kgets_client) {
			/* Switch page directories into the caller so we can write to its buffers */
			switch_page_directory(kgets_client->thread.page_directory);
		}
		for (int i = kgets_collected; i > kgets_offset; --i) {
			kgets_buffer[i] = kgets_buffer[i-1];
		}
		if (kgets_collected < kgets_want) {
			kgets_buffer[kgets_offset] = ch;
			kgets_buffer[++kgets_collected] = '\0';
			kgets_offset++;
		}
		for (int i = kgets_offset - 1; i < kgets_collected; ++i) {
			kwrite(kgets_buffer[i]);
		}
		for (int i = kgets_offset; i < kgets_collected; ++i) {
			kwrite(27);
			kwrite(91);
			kwrite(68);
		}
		bochs_redraw_cursor();
		IRQ_ON;
	} else {
		kwrite(ch);
		if (kgets_collected < kgets_want) {
			IRQ_OFF;
			if (current_process != kgets_client) {
				/* Switch page directories into the caller so we can write to its buffers */
				switch_page_directory(kgets_client->thread.page_directory);
			}
			kgets_buffer[kgets_collected] = ch;
			kgets_buffer[++kgets_collected] = '\0';
			kgets_offset++;
			IRQ_ON;
		}
	}
}

void kgets_redraw_buffer() {
	kprintf(kgets_buffer);
	for (int i = kgets_offset; i < kgets_collected; ++i) {
		kwrite(27);
		kwrite(91);
		kwrite(68);
	}
}

/**
 * Synchronously get a string from the keyboard.
 *
 * @param buffer Where to put it
 * @param size   Maximum size of the string to receive.
 */
int
kgets(
		char *buffer,
		int size
	 ) {
	/* Reset the buffer */
	kgets_buffer    = buffer;
	kgets_collected = 0;
	kgets_want      = size;
	kgets_newline   = 0;
	kgets_buffer[0] = '\0';
	kgets_offset    = 0;
	/* Assign the keyboard handler */
	kgets_client    = current_process;
	keyboard_buffer_handler = kgets_handler;
	while ((kgets_collected < size) && (!kgets_newline)) {
		/* Wait until the buffer is ready */
		PAUSE;
	}
	/* Fix any missing nulls */
	buffer[kgets_collected] = '\0';
	/* Disable the buffer */
	keyboard_buffer_handler = NULL;
	kgets_redraw_func = NULL;
	kgets_tab_complete_func = NULL;
	/* Return the string */
	return kgets_collected;
}

/*
 * vim:tabstop=4
 * vim:noexpandtab
 */
