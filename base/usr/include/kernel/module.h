#pragma once

struct Module {
	const char * name;
	int (*init)(int argc, char * argv[]);
	int (*fini)(void);
};
