/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 *
 * menu - Display a menu file and print actions
 *
 * This is a demo of the menu library, and can be used by scripts
 * to display menus. It may be broken without a root window for
 * the menus to display on, though?
 */
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <sys/types.h>

#include <toaru/yutani.h>
#include <toaru/graphics.h>
#include <toaru/sdf.h>
#include <toaru/hashmap.h>
#include <toaru/list.h>
#include <toaru/menu.h>

static yutani_t * yctx;

static void _action_callback(struct MenuEntry * self) {
	struct MenuEntry_Normal * _self = (void *)self;

	fprintf(stdout, "%s\n", _self->action);
	exit(0);
}

int main(int argc, char * argv[]) {

	if (argc < 2) {
		fprintf(stderr, "%s: expected argument\n", argv[0]);
		return 1;
	}

	yctx = yutani_init();

	/* Create menu from file. */
	struct MenuSet * menu = menu_set_from_description(argv[1], _action_callback);
	menu_show(menu_set_get_root(menu), yctx);

	while (1) {
		yutani_msg_t * m = yutani_poll(yctx);
		if (menu_process_event(yctx, m)) {
			return 1;
		}
		free(m);
	}

	return 0;
}
