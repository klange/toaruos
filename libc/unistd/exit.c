#include <unistd.h>
#include <stdlib.h>

#include <libc/internal.h>
#include <libc/stdio/stdio_internal.h>

extern void _fini();

void exit(int val) {
	__atexit_run();
	_fini();
	__stdio_cleanup();
	_exit(val);
}
