/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014 Kevin Lange
 */
#include <system.h>
#include <hashmap.h>
#include <module.h>
#include <logging.h>

extern int a_function(void);

static int hello(void) {
	debug_print(NOTICE, "Calling a_function from other module.");
	a_function();
	return 0;
}

static int goodbye(void) {
	debug_print(NOTICE, "Goodbye!");
	return 0;
}

MODULE_DEF(testb, hello, goodbye);
MODULE_DEPENDS(test);

