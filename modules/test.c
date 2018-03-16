/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014 Kevin Lange
 */
#include <system.h>
#include <hashmap.h>
#include <module.h>
#include <logging.h>
#include <printf.h>

#include <mod/shell.h>

extern char * special_thing;

char test_module_string[] = "I am a char[] in the module.";
char * test_module_string_ptr = "I am a char * in the module.";

int a_function(void) {
	debug_print(WARNING, ("I am an exported function in the module."));
	return 42;
}

DEFINE_SHELL_FUNCTION(test_mod, "A function installed by a module!") {
	fprintf(tty, "Hello world!\n");
	return 0;
}

static int hello(void) {
	debug_print(NOTICE, special_thing);
	a_function();
	debug_print(NOTICE, test_module_string);
	debug_print(NOTICE, test_module_string_ptr);

	hashmap_t * map = hashmap_create(10);

	debug_print(NOTICE, "Inserting into hashmap...");

	hashmap_set(map, "hello", (void *)"cake");
	debug_print(NOTICE, "getting value: %s", hashmap_get(map, "hello"));

	hashmap_free(map);
	free(map);

	debug_shell_install(&shell_test_mod_desc);
	BIND_SHELL_FUNCTION(test_mod);

	return 25;
}

static int goodbye(void) {
	debug_print(NOTICE, "Goodbye!");
	return 0;
}

MODULE_DEF(test, hello, goodbye);
MODULE_DEPENDS(debugshell);

