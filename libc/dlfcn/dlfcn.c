/*
 * Stub library for providing dlopen, dlclose, dlsym.
 */

#include <stdlib.h>

static char * error = "dlopen functions not available";

void * __attribute__((weak)) dlopen(const char * filename, int flags) {
	return NULL;
}

int __attribute__((weak)) dlclose(void * handle) {
	return -1;
}

void * dlsym(void * handle, const char * symbol) {
	return NULL;
}

char * dlerror(void) {
	return error;
}

int __cxa_atexit(void (*fn)(void *), void * arg, void *d) {
	return 0;
}

void __ld_symbol_table(void) { }
void __ld_objects_table(void) { }
