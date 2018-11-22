/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 */

/* This definition will eventually change with the rest of the widget toolkit */
struct TTKButton {
	int x;
	int y;
	int width;
	int height;
	char * title;
	int hilight;
};

extern void ttk_button_draw(gfx_context_t * ctx, struct TTKButton * button);
