#define _TOARU_SOURCE
#include <sched.h>
#include <stdlib.h>
#include <stdio.h>
#include <err.h>

int main(int argc, char * argv[]) {
	if (argc < 2) {
		fprintf(stderr, "usage: %s pid [affinity]\n", argv[0]);
		return 1;
	}

	uint64_t cores = 0;
	pid_t p = atoi(argv[1]);

	if (argc > 2 && sched_setaffinity_t(p, strtoul(argv[2], NULL, 0)) < 0) err(1, "sched_setaffinity");

	if (sched_getaffinity_t(p, &cores) < 0) err(1, "sched_getaffinity");

	printf("%d: %#zx\n", p, cores);

	return 0;
}
