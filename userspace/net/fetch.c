#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "lib/http_parser.h"

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

int callback_header_field (http_parser *p, const char *buf, size_t len) {
	fprintf(stderr, "Header field: %.*s\n", len, buf);
	return 0;
}

int callback_header_value (http_parser *p, const char *buf, size_t len) {
	fprintf(stderr, "Header value: %.*s\n", len, buf);
	return 0;
}

int callback_body (http_parser *p, const char *buf, size_t len) {
	fwrite(buf, 1, len, stdout);
	return 0;
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

	http_parser_settings settings;
	memset(&settings, 0, sizeof(settings));
	settings.on_header_field = callback_header_field;
	settings.on_header_value = callback_header_value;
	settings.on_body = callback_body;

	http_parser parser;
	http_parser_init(&parser, HTTP_RESPONSE);

	while (!feof(f)) {
		char buf[1024];
		memset(buf, 0, sizeof(buf));
		size_t r = fread(buf, 1, 1024, f);
		http_parser_execute(&parser, &settings, buf, r);
	}

	fflush(stdout);

	return 0;
}
