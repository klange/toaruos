#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define SIZE 512

struct http_req {
	char domain[SIZE];
	char path[SIZE];
};

void parse_url(char * d, struct http_req * r) {
	if (strstr(d, "http://") == d) {

		d += strlen("http://");

		char * s = strstr(d, "/");
		if (!s) {
			strcpy(r->domain, d);
			strcpy(r->path, "");
		} else {
			*s = 0;
			s++;
			strcpy(r->domain, d);
			strcpy(r->path, s);
		}
	} else {
		fprintf(stderr, "sorry, can't parse %s\n", d);
		exit(1);
	}
}


int main(int argc, char * argv[]) {

	if (argc < 2) return 1;

	struct http_req my_req;
	parse_url(argv[1], &my_req);

	char file[100];
	sprintf(file, "/dev/net/%s", my_req.domain);

	FILE * f = fopen(file,"r+");

	if (!f) {
		fprintf(stderr, "Nope.\n");
		return 1;
	}

	fprintf(f,
		"GET /%s HTTP/1.0\r\n"
		"User-Agent: curl/7.35.0\r\n"
		"Host: %s\r\n"
		"Accept: */*\r\n"
		"\r\n", my_req.path, my_req.domain);

	while (!feof(f)) {
		char buf[10];
		memset(buf, 0, sizeof(buf));
		size_t r = fread(buf, 1, 10, f);
		fwrite(buf, 1, r, stdout);
	}

	fflush(stdout);

	return 0;
}
