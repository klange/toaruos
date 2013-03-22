#include <stdlib.h>
#include <stdio.h>

int main(int arch, char * argv[]) {
	fprintf(stderr, "Calling system(\"echo hello world\")\n");
	int ret = system("echo hello world");
	fprintf(stderr, "Done. Returned %d.\n", ret);
}
