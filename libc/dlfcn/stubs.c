#include <libc/dlfcn/internal.h>

int __cxa_atexit(void (*fn)(void *), void * arg, void *d) {
	return 0;
}

void __register_frame_info(void) {}
void __deregister_frame_info(void) {}
void _ITM_registerTMCloneTable(void) {}
void _ITM_deregisterTMCloneTable(void) {}
void __cxa_finalize(void) {}


