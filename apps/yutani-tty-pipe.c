#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>

#include <toaru/yutani.h>
#include <toaru/yutani-internal.h>

yutani_t * yctx;
int quiet = 0;

int show_resolution(void) {
	if (!yctx) {
		if (!quiet) printf("(not connected)\n");
		return 1;
	}
	printf("%dx%d\n", (int)yctx->display_width, (int)yctx->display_height);
	return 0;
}

int reload(void) {
	if (!yctx) {
		if (!quiet) printf("(not connected)\n");
		return 1;
	}
	yutani_special_request(yctx, NULL, YUTANI_SPECIAL_REQUEST_RELOAD);
	return 0;
}

struct termios old;

void set_unbuffered() {
	tcgetattr(fileno(stdin), &old);
	struct termios new = old;
	new.c_lflag &= (~ICANON & ~ECHO);
	tcsetattr(fileno(stdin), TCSAFLUSH, &new);
}


int main(int argc, char * argv[]) {
	yctx = yutani_init();

	if (!yctx) {
		fprintf(stderr, "not connected; did you set $DISPLAY?\n");
		return 1;
	}

	set_unbuffered();

	int c;
	while ((c = fgetc(stdin))) {
		key_event_t event = {0};
		key_event_state_t state = {0};

		event.keycode = c;
		event.key = c;

		switch (c) {
			case 27:
				event.keycode = KEY_ESCAPE;
				event.key = KEY_ESCAPE;
				break;
			/* Either of the backspace keys */
			case 8:
			case 0x7f:
				event.keycode = 8;
				event.key = 8;
				break;
			/* Any of \r or \n */
			case '\r':
			case '\n':
				event.keycode = '\n';
				event.key = '\n';
				break;
		}

		event.action = KEY_ACTION_DOWN;

		yutani_msg_buildx_key_event_alloc(m_);
		yutani_msg_buildx_key_event(m_, 0, &event, &state);
		yutani_msg_send(yctx, m_);

		event.action = KEY_ACTION_UP;
		yutani_msg_buildx_key_event(m_, 0, &event, &state);
		yutani_msg_send(yctx, m_);
	}

	return 0;
}

