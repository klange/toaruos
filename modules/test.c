extern char * special_thing;

int a_function(void (*callback)(char *)) {
	callback("Hello world!");
	return 42;
}

int b_function(void (*callback)(char *)) {
	callback("I can do this, too!\n");
	callback(special_thing);
	return 25;
}

