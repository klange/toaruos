#include <unistd.h>
#include <stdlib.h>

#include "../internal.h"

void exit(int val) {
	__atexit_run();
	_exit(val);
}
