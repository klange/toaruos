#include <stdio.h>
#include <string.h>

extern char * _username;

int return_42(void) {
	fprintf(stderr, "I am a dynamically loaded shared object. pid = %d\n", getpid());
	return 42;
}
