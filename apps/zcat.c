#include <unistd.h>
#include <stdlib.h>

int main(int argc, char * argv[]) {
	char ** args = malloc(sizeof(char*) * (argc + 2)); /* For -c and terminating NULL */
	args[0] = "gunzip";
	args[1] = "-c";
	for (int i = 1; i < argc; ++i) {
		args[i+1] = argv[i];
	}
	args[argc+1] = NULL;
	return execvp("gunzip", args);
}
