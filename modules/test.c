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
	return 25;
}

