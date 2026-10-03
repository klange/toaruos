#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char * argv[]) {
	char *ncount = "1000";
	if (argc == 2) {
		int count = atoi(argv[1]);
		if (count <= 0) return system("cat /proc/meminfo");
		asprintf(&ncount, "%d", count - 1);
	} else {
		system("cat /proc/meminfo");
	}
	char * args[] = {"test-lotsofexecs", ncount, NULL};
	return execv("/bin/test-lotsofexecs", args);
}
