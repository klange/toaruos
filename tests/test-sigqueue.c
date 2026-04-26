#include <stdlib.h>
#include <stdio.h>
#include <signal.h>

int main (int argc, char * argv[]) {
	if (argc < 4) return 1;

	pid_t p = atoi(argv[1]);
	int   s = atoi(argv[2]);
	int   v = atoi(argv[3]);

	union sigval val;
	val.sival_int = v;
	return sigqueue(p, s, val);
}
