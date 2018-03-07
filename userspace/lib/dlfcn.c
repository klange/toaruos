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
