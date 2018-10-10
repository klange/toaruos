#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char * argv[]) {
	size_t req = mbstowcs(NULL, argv[1], 0);
	wchar_t * dest = malloc(sizeof(wchar_t) * req);
	mbstowcs(dest, argv[1], req+1);

	for (size_t i = 0; i < req; ++i) {
		char tmp[8];
		wchar_t in[] = {dest[i], L'\0'};
		wcstombs(tmp, in, 8);
		fprintf(stdout, "U+%4x %s\n", dest[i], tmp);
	}

	return 0;
}
