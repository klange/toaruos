#include <stdlib.h>

static void (*_atexit_handlers[32])(void) = {NULL};
static int _atexit_count = 0;

void _handle_atexit(void) {
	if (!_atexit_count) return;
	do {
		_atexit_count--;
		_atexit_handlers[_atexit_count]();
	} while (_atexit_count);
}

int atexit(void (*h)(void)) {
	if (_atexit_count == ATEXIT_MAX) return 1;
	_atexit_handlers[_atexit_count++] = h;
	return 0;
}

