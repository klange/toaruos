/*
 * Based on a demo in the Linux manpages.
 */
#include <stdio.h>
#include <stdlib.h>

#include "lib/dlfcn.h"

int main(int argc, char * argv[]) {

	void * handle;
	double (*cosine)(double);
	char * error;

	handle = dlopen("libm.so", 0); /* TODO constants */
	if (!handle) {
		fprintf(stderr, "%s\n", dlerror());
		return 1;
	}

	/* Clear error */
	dlerror();

	cosine = (double (*)(double))dlsym(handle, "cos");

	error = dlerror();
	if (error != NULL) {
		fprintf(stderr, "%s\n", error);
		return 1;
	}

	printf("%f\n", (*cosine)(2.0));
	dlclose(handle);

	return 0;
}
