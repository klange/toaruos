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

