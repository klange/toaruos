#pragma once

#include <kernel/hashmap.h>

struct Module {
	const char * name;
	int (*init)(int argc, char * argv[]);
	int (*fini)(void);
};

struct LoadedModule {
	struct Module * metadata;
	uintptr_t baseAddress;
};

hashmap_t * modules_get_list(void);
