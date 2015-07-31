#include <stdio.h>

int main(int argc, char * argv[]) {

	if (argc < 2) return 1;

	char file[100];
	sprintf(file, "/dev/net/%s", argv[1]);

	FILE * f = fopen(file,"r+");

	if (!f) {
		fprintf(stderr, "Nope.\n");
		return 1;
	}

	fprintf(f,
		"GET / HTTP/1.0\r\n"
		"User-Agent: curl/7.35.0\r\n"
		"Host: %s\r\n"
		"Accept: */*\r\n"
		"\r\n", argv[1]);

	while (!feof(f)) {
		char buf[4096];
		fgets(buf, 4096, f);
		fprintf(stdout, "%s", buf);
	}

	return 0;
}
