#include <module.h>
#include <printf.h>
#include <mod/shell.h>

#include "lwip/init.h"

DEFINE_SHELL_FUNCTION(netif_test, "networking stuff") {
	fprintf(tty, "Initializing LWIP...\n");

	lwip_init();

	fprintf(tty, "Hello world?\n");

	return 0;
}

static int init(void) {
	BIND_SHELL_FUNCTION(netif_test);
	return 0;
}

static int fini(void) {
	return 0;
}

MODULE_DEF(netif, init, fini);
MODULE_DEPENDS(debugshell);
