#include <kernel/system.h>
#include <kernel/printf.h>
#include <kernel/module.h>
#include <kernel/logging.h>
#include <kernel/types.h>

#include "../lib/termemu.c"

static unsigned short * textmemptr = (unsigned short *)0xB8000;
static void placech(unsigned char c, int x, int y, int attr) {
	unsigned short *where;
	unsigned att = attr << 8;
	where = textmemptr + (y * 80 + x);
	*where = c | att;
}

static char vga_to_ansi[] = {
	0, 4, 2, 6, 1, 5, 3, 7,
	8,12,10,14, 9,13,11,15
};

static int current_fg = 0x07;
static int current_bg = 0x10;
static int cur_x = 0;
static int cur_y = 0;

term_state_t * ansi_state = NULL;

static int write_string(char * s) {
	int written = 0;
	while (*s) {
		switch (*s) {
			case '\n':
				cur_x = 0;
				cur_y++;
				break;
			case '\b':
				if (cur_x > 0) cur_x--;
				placech(' ', cur_x, cur_y, (vga_to_ansi[current_fg] & 0xF) | (vga_to_ansi[current_bg] << 4));
				break;
			default:
				placech(*s, cur_x, cur_y, (vga_to_ansi[current_fg] & 0xF) | (vga_to_ansi[current_bg] << 4));
				cur_x++;
				break;
		}
		if (cur_x == 80) {
			cur_x = 0;
			cur_y++;
		}
		if (cur_y == 25) {
			memmove(textmemptr, (textmemptr + 80), sizeof(unsigned short) * 80 * 24);
			memset(textmemptr + 80 * 24, 0x00, 80 * sizeof(unsigned short));
			cur_y = 24;
		}
		s++;
		written++;
	}
	return written;
}

static void term_write(char c) {
	char foo[] = {c,0};
	write_string(foo);
}

static uint32_t vga_write(fs_node_t * node, uint64_t offset, uint32_t size, uint8_t *buffer) {
	/* XXX do some terminal processing like we did in the old days */
	size_t i = 0;
	while (*buffer && i < size) {
		ansi_put(ansi_state, *buffer);
		buffer++;
		i++;
	}
	return i;
}

static fs_node_t _vga_fnode = {
	.name  = "vga_log",
	.write  = vga_write,
};

static void term_scroll(int how_much) {
	for (int i = 0; i < how_much; ++i) {
		memmove(textmemptr, (textmemptr + 80), sizeof(unsigned short) * 80 * 24);
		memset(textmemptr + 80 * 24, 0x00, 80 * sizeof(unsigned short));
	}
}

static void term_set_cell(int x, int y, uint32_t c) {
	placech(c, x, y, (vga_to_ansi[current_fg] & 0xF) | (vga_to_ansi[current_bg] << 4));
}

static void term_set_csr(int x, int y) {
	cur_x = x;
	cur_y = y;
}

static int term_get_csr_x() {
	return cur_x;
}

static int term_get_csr_y() {
	return cur_y;
}

static void term_set_csr_show(int on) {
	return;
}

static void term_set_colors(uint32_t fg, uint32_t bg) {
	current_fg = fg;
	current_bg = bg;
}

static void term_redraw_cursor() {
	return;
}

static void input_buffer_stuff(char * str) {
	return;
}

static void set_title(char * c) {
	/* Do nothing */
}

static void term_clear(int i) {
	memset(textmemptr, 0x00, sizeof(unsigned short) * 80 * 25);
}

int unsupported_int(void) { return 0; }
void unsupported(int x, int y, char * data) { }

term_callbacks_t term_callbacks = {
	term_write,
	term_set_colors,
	term_set_csr,
	term_get_csr_x,
	term_get_csr_y,
	term_set_cell,
	term_clear,
	term_scroll,
	term_redraw_cursor,
	input_buffer_stuff,
	set_title,
	unsupported,
	unsupported_int,
	unsupported_int,
	term_set_csr_show,
	NULL,
	NULL,
};


static int vgadbg_init(void) {

	memset(textmemptr, 0x00, sizeof(unsigned short) * 80 * 25);

	ansi_state = ansi_init(ansi_state, 80, 25, &term_callbacks);

	debug_file = &_vga_fnode;
	debug_level = 1;

	write_string("VGA Debug Logging is enabled.\n");

	return 0;
}

static int vgadbg_fini(void) {
	return 0;
}

MODULE_DEF(vgalog, vgadbg_init, vgadbg_fini);
