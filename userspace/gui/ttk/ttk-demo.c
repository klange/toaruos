/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013 Kevin Lange
 */
/*
 *
 * Toolkit Demo and Development Application
 *
 */
#include <stdlib.h>
#include <assert.h>

#include "gui/ttk/ttk.h"

int main (int argc, char ** argv) {

	ttk_initialize();
	ttk_window_t * main_window = ttk_window_new("TTK Demo", 500, 500);

	return ttk_run(main_window);
}
