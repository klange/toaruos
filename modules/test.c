#include <system.h>
#include <hashmap.h>

extern char * special_thing;

char test_module_string[] = "I am a char[] in the module.\n";
char * test_module_string_ptr = "I am a char * in the module.\n";

static int a_function(void (*callback)(char *)) {
	callback("I am a static function in the module.\n");
	return 42;
}

int b_function(void (*callback)(char *)) {
	callback("I am a global function in a module!\n");
	callback(special_thing);
	a_function(callback);
	callback(test_module_string);
	callback(test_module_string_ptr);

	hashmap_t * map = hashmap_create(10);

	callback("Inserting into hashmap...\n");

	hashmap_set(map, "hello", (void *)"cake\n");
	callback("getting value...\n");

	callback(hashmap_get(map, "hello"));
	hashmap_free(map);
	free(map);

	return 25;
}

