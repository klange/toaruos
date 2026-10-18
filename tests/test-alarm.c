#include <unistd.h>
#include <stdio.h>

int main(int argc, char * argv[]) {
	alarm(5);

	sleep(1);

	unsigned int n = alarm(2);
	fprintf(stderr, "n = %d\n", n);

	while (1) {
		fgetc(stdin);
	}

	return 0;
}
