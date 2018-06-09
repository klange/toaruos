
static int read_scancode(void) {
	while (!(inportb(0x64) & 1));
	int out;
	while (inportb(0x64) & 1) {
		out = inportb(0x60);
	}
	return out;
}
