#include <unistd.h>

void exit(int val) {
	// TODO call exit cleanup handlers (including flushing buffers?)
	_exit(val);
}
