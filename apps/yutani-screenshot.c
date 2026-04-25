/**
 * @brief yutani-screenshot - make the compositor take a screenshot
 *
 * This injects a screenshot key press into the compositor.
 * This only works because the compositor accepts key events
 * from applications as equivalent to local key presses, due
 * to a holdover from a legacy design. This is subject to
 * change at some point in the future.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2026 K. Lange
 */
#include <stdio.h>
#include <unistd.h>
#include <getopt.h>

#include <toaru/yutani.h>
#include <toaru/yutani-internal.h>
#include <toaru/kbd.h>

int show_usage(int argc, char * argv[]) {
#define X_S "\033[3m"
#define X_E "\033[0m"
	fprintf(stderr,
			"%s - make the compositor take a screenshot\n"
			"\n"
			"usage: %s [-w]\n"
			"\n"
			" -w  --window " X_S "take a screenshot of the focused window" X_E "\n"
			"     --help   " X_S "show this help text" X_E "\n"
			"\n", argv[0], argv[0]);
	return 1;
}

int main(int argc, char * argv[]) {
	int of_window = 0;
	int opt;

	struct option long_opts[] = {
		{"window",no_argument,0,'w'},
		{"help",no_argument,0,'?'},
		{0,0,0,0},
	};

	while ((opt = getopt_long(argc, argv, "w", long_opts, NULL)) != -1) {
		switch (opt) {
			case 'w':
				of_window = 1;
				break;
			case '?':
				return show_usage(argc,argv);
		}
	}

	yutani_t * yctx = yutani_init();
	if (!yctx) {
		fprintf(stderr, "%s: not connected\n", argv[0]);
		return 1;
	}

	key_event_t event = {KEY_PRINT_SCREEN, of_window ? KEY_MOD_LEFT_SHIFT : 0, KEY_ACTION_DOWN, 0};
	key_event_state_t state = {0};

	yutani_msg_buildx_key_event_alloc(response);
	yutani_msg_buildx_key_event(response, 0, &event, &state);
	yutani_msg_send(yctx, response);

	return 0;
}

