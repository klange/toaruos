#include <stdio.h>
#include <sys/select.h>

int main(int argc, char * argv[]) {

	fd_set set;
	FD_ZERO(&set);
	FD_SET(0, &set);

	struct timeval tv;
	tv.tv_sec = 2;
	tv.tv_usec = 0;

	int r = select(1, &set, NULL, NULL, &tv);

	fprintf(stderr, "%d\n", r);

	return 0;
}
