#include <unistd.h>
#include <stdio.h>

int main(int argc, char * argv[]) {
	alarm(2);

	while (1) {
		fgetc(stdin);
	}

	return 0;
}
