#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <toaru/kbd.h>

static unsigned short * textmemptr = (unsigned short *)0xB8000;
static void placech(unsigned char c, int x, int y, int attr) {
	unsigned short *where;
	unsigned att = attr << 8;
	where = textmemptr + (y * 80 + x);
	*where = c | att;
}

static void clear_screen(void) {
	for (int y = 0; y < 24; ++y) {
		for (int x = 0; x < 80; ++x) {
			placech(' ', x, y, 0); /* Clear */
		}
	}
}

#define BUF_SIZE 4096
static char keys[256] = {0};

static void redraw(void) {
	int i = 0;
	for (int c = 'a'; c <= 'z'; ++c, i += 2) {
		placech(c, i, 0, keys[c] ? 0x2 : 0x7);
	}
}

static void print_scancode(unsigned int sc) {
	char buf[10];
	sprintf(buf, "%d", sc);

	int i = 0;
	for (char * c = buf; *c; ++c, ++i) {
		placech(*c, i, 1, 0x7);
	}
	for (; i < 4; ++i) {
		placech(' ', i, 1, 0x7);
	}
}

int main(int argc, char * argv[]) {
	clear_screen();
	int kfd = open("/dev/kbd", O_RDONLY);
	key_event_t event;
	key_event_state_t kbd_state = {0};

	while (1) {
		unsigned char buf[BUF_SIZE];
		int r = read(kfd, buf, BUF_SIZE);
		for (int i = 0; i < r; ++i) {
			kbd_scancode(&kbd_state, buf[i], &event);
			if (event.keycode >= 'a' && event.keycode < 'z') {
				keys[event.keycode] = (event.action == KEY_ACTION_DOWN);
			}
			print_scancode(buf[i]);
		}
		redraw();

	}

}
