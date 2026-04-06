#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

void handler(int sig) {
	fprintf(stderr, "oh no\n");
}

int main(int argc, char * argv[]) {
	signal(SIGABRT, handler);
	abort();
	fprintf(stderr, "Unreachable?\n");
}
