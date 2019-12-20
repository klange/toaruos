#include <stdio.h>
#include <time.h>

int main(int argc, char * argv[]) {
	time_t i = 1576000000;
	while (i < 2000000000) {
		struct tm * t = localtime(&i);

		if (t->tm_sec < 0 || t->tm_sec >= 60) fprintf(stderr, "Erroneous value at %ld: sec = %d\n", i, t->tm_sec);
		if (t->tm_min < 0 || t->tm_min >= 60) fprintf(stderr, "Erroneous value at %ld: min = %d\n", i, t->tm_min);
		if (t->tm_hour < 0 || t->tm_hour >= 24) fprintf(stderr, "Erroneous value at %ld (%s) hour = %d\n", i, asctime(t), t->tm_hour);

		i++;
	}
	return 0;
}
