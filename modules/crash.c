#include <module.h>
#include <mod/shell.h>

DEFINE_SHELL_FUNCTION(crash, "Dereference NULL.") {
	int x = *((int *)0x20000000 );
	return x;
}

static int crash_init(void) {
	BIND_SHELL_FUNCTION(crash);
	return 0;
}

static int crash_fini(void) {
	return 0;
}

MODULE_DEF(crash, crash_init, crash_fini);
MODULE_DEPENDS(debugshell);
