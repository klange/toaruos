#include <stdio.h>
#include <stdlib.h>

#include "lib/trace.h"
#define TRACE_APP_NAME "ld.so"

#include "../kernel/include/elf.h"

int _ld_so_main(int argc, char * argv[]) {
	return 0;
}

int main(int argc, char * argv[]) {

	TRACE("Hello world.");

	char * env_ld_library_path = getenv("LD_LIBRARY_PATH");
	char * env_ld_preload = getenv("LD_PRELOAD");


	return 0;
}
