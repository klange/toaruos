#include <kernel/printf.h>

static int init(int argc, char * argv[]) {
	printf("Hello, world.\n");
	return 0;
}

static int fini(void) {
	return 0;
}

struct Module {
	const char * name;
	int (*init)(int argc, char * argv[]);
	int (*fini)(void);
} module_info_test = {
	.name = "test",
	.init = init,
	.fini = fini,
};

