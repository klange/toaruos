#include <unistd.h>
#include <stdlib.h>

extern void __atexit_run(void);
void exit(int val) {
	__atexit_run();
	_exit(val);
}
