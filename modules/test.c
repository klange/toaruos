extern char * special_thing;

char module_name[] = "test-string-please-ignore";

static int a_function(void (*callback)(char *)) {
	callback("Hello world!");
	return 42;
}

int b_function(void (*callback)(char *)) {
	callback("I can do this, too!\n");
	callback(special_thing);
	a_function(callback);
	return 25;
}

