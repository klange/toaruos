#include <stdio.h>
#include <string.h>


__attribute__((constructor))
static void butts(void) {
	fprintf(stderr, "I'm a constructor!\n");
}

extern char * _username;

int return_42(void) {
	fprintf(stderr, "I am a dynamically loaded shared object. pid = %d\n", getpid());
	return 42;
}
