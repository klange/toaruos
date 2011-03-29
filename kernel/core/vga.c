#include <system.h>

/*
 * Text pointer, background, foreground
 */
unsigned short * textmemptr;
int attrib = 0x0F;
int csr_x = 0, csr_y = 0, use_serial = 1, use_csr = 1;
int old_x = 0, old_y = 0, old_s = 1, old_csr = 1;

/*
 * scroll
 * Scroll the screen
 */
void
scroll() {
	unsigned blank, temp;
	blank = 0x20 | (attrib << 8);
	if (csr_y >= 25) {
		/*
		 * Move the current text chunk that makes up the screen
		 * back in the buffer by one line.
		 */
		temp = csr_y - 25 + 1;
		memcpy(textmemptr, textmemptr + temp * 80, (25 - temp) * 80 * 2);
		/*
		 * Set the chunk of memory that occupies
		 * the last line of text to the blank character
		 */
		memsetw(textmemptr + (25 - temp) * 80, blank, 80);
		csr_y = 25 - 1;
	}
}

void
set_serial(int on) {
	use_serial = on;
}

void
set_csr(int on) {
	use_csr = on;
}

void
store_csr() {
	old_x = csr_x;
	old_y = csr_y;
	old_s = use_serial;
	old_csr = use_csr;
}

void
restore_csr() {
	csr_x = old_x;
	csr_y = old_y;
	use_serial = old_s;
	use_csr = old_csr;
}

/*
 * move_csr
 * Update the hardware cursor
 */
void
move_csr() {
	if (!use_csr) return;
	unsigned temp;
	temp = csr_y * 80 + csr_x;
	
	/*
	 * Write stuff out.
	 */
	outportb(0x3D4, 14);
	outportb(0x3D5, temp >> 8);
	outportb(0x3D4, 15);
	outportb(0x3D5, temp);
}

/*
 * place_csr(x, y)
 */
void
place_csr(
		uint32_t x,
		uint32_t y
		) {
	csr_x = x;
	csr_y = y;
	move_csr();
}

/*
 * cls
 * Clear the screen
 */
void
cls() {
	unsigned blank;
	int i;
	blank = 0x20 | (attrib << 8);
	for (i = 0; i < 25; ++i) {
		memsetw(textmemptr + i * 80, blank, 80);
	}
	csr_x = 0;
	csr_y = 0;
	move_csr();
	if (bochs_resolution_x) {
		bochs_term_clear();
	}
}

/*
 * placech
 * Put a character in a particular cell with the given attributes.
 */
void
placech(
		unsigned char c,
		int x,
		int y,
		int attr
	   ) {
	unsigned short *where;
	unsigned att = attr << 8;
	where = textmemptr + (y * 80 + x);
	*where = c | att;
}

/*
 * writechf
 * Force write the given character.
 */
void
writechf(
		unsigned char c
		) {
	placech(c, csr_x, csr_y, attrib);
	csr_x++;
	if (csr_x >= 80) {
		csr_x = 0;
		++csr_y;
	}
	scroll();
	move_csr();
}



/*
 * writech
 * Write a character to the screen.
 */
void
writech(
		unsigned char c
	   ) {
	unsigned short *where;
	unsigned att = attrib << 8;
	if (use_serial) {
		serial_send(c);
	}
	if (bochs_resolution_x) {
		bochs_write(c);
		return;
	}
	if (c == 0x08) {
		/* Backspace */
		if (csr_x != 0) csr_x--;
	} else if (c == 0x09) {
		/* Tab */
		csr_x = (csr_x + 8) & ~(8 - 1);
	} else if (c == '\r') {
		/* Carriage return */
		csr_x = 0;
	} else if (c == '\n') {
		/* New line */
		csr_x = 0;
		csr_y++;
	} else if (c >= ' ') {
		where = textmemptr + (csr_y * 80 + csr_x);
		*where = c | att;
		csr_x++;
	}

	if (csr_x >= 80) {
		csr_x = 0;
		csr_y++;
	}
	scroll();
	move_csr();
}

/*
 * puts
 * Put string to screen
 */
void
puts(
		char * text
	){ 
	int i;
	int len = strlen(text);
	for (i = 0; i < len; ++i) {
		writech(text[i]);
	}
}

char vga_to_ansi[] = {
    0, 4, 2, 6, 1, 5, 3, 7,
    8,12,10,14, 9,13,11,15
};

/*
 * settextcolor
 * Sets the foreground and background color
 */
void
settextcolor(
		unsigned char forecolor,
		unsigned char backcolor
		) {
	attrib = (backcolor << 4) | (forecolor & 0x0F);
	forecolor = vga_to_ansi[forecolor];
	backcolor = vga_to_ansi[backcolor];
	if (use_serial) {
		serial_send('\033');
		serial_send('[');
		serial_send('3');
		serial_send(forecolor % 8 + '0');
		serial_send('m');
	}
	bochs_set_colors(forecolor, backcolor);
}

/*
 * resettextcolor
 * Reset the text color to white on black
 */
void
resettextcolor() {
	settextcolor(7,0);
	if (use_serial) {
		serial_send('\033');
		serial_send('[');
		serial_send('0');
		serial_send('m');
	}
	bochs_reset_colors();
}

void
brighttextcolor() {
	settextcolor(15,0);
}

/*
 * init_video
 * Initialize the VGA driver.
 */
void init_video() {
	textmemptr = (unsigned short *)0xB8000;
	csr_y = 10;
	move_csr();
}
