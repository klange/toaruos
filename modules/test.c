#include <kernel/printf.h>
#include <kernel/module.h>

static int init(int argc, char * argv[]) {
	printf("Hello, modules.\n");
	return 0;
}

static int fini(void) {
	return 0;
}

struct Module metadata = {
	.name = "test",
	.init = init,
	.fini = fini,
};

