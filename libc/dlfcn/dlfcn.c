/*
 * Stub library for providing dlopen, dlclose, dlsym.
 */

#include <stdlib.h>

static char * error = "dlopen functions not available";

int __attribute__((weak)) dlclose(void * handle) {
	return -1;
}

char * dlerror(void) {
	return error;
}

int __cxa_atexit(void (*fn)(void *), void * arg, void *d) {
	return 0;
}

