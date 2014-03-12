#include <system.h>
#include <hashmap.h>
#include <module.h>
#include <logging.h>

extern char * special_thing;

char test_module_string[] = "I am a char[] in the module.";
char * test_module_string_ptr = "I am a char * in the module.";

static int a_function(void) {
	debug_print(WARNING, ("I am a static function in the module."));
	return 42;
}

int b_function(void) {
	debug_print(NOTICE, "I am a global function in a module!");
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

	return 25;
}

int goodbye(void) {
	debug_print(NOTICE, "Goodbye!");
	return 0;
}

MODULE_DEF(test, b_function, goodbye);

