#include <unistd.h>
#include <stdlib.h>

void exit(int val) {
	_handle_atexit();
	_exit(val);
}
