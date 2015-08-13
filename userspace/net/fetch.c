/* vim: ts=4 sw=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2015 Kevin Lange
 *
 * fetch - Retreive documents from HTTP servers.
 *
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>

#include "lib/http_parser.h"

#define SIZE 512

struct http_req {
	char domain[SIZE];
	char path[SIZE];
};

struct {
	int show_headers;
	const char * output_file;
	const char * cookie;
	FILE * out;
} fetch_options = {0};

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
	if (fetch_options.show_headers) {
		fprintf(stderr, "Header field: %.*s\n", len, buf);
	}
	return 0;
}

int callback_header_value (http_parser *p, const char *buf, size_t len) {
	if (fetch_options.show_headers) {
		fprintf(stderr, "Header value: %.*s\n", len, buf);
	}
	return 0;
}

int callback_body (http_parser *p, const char *buf, size_t len) {
	fwrite(buf, 1, len, fetch_options.out);
	return 0;
}

int usage(char * argv[]) {
	fprintf(stderr, "Usage: %s [-h] [-c cookie] [-o file] url\n", argv[0]);
	return 1;
}


int main(int argc, char * argv[]) {

	int opt;

	while ((opt = getopt(argc, argv, "?c:ho:")) != -1) {
		switch (opt) {
			case '?':
				return usage(argv);
			case 'c':
				fetch_options.cookie = optarg;
				break;
			case 'h':
				fetch_options.show_headers = 1;
				break;
			case 'o':
				fetch_options.output_file = optarg;
				break;
		}
	}

	if (optind >= argc) {
		return usage(argv);
	}

	struct http_req my_req;
	parse_url(argv[optind], &my_req);

	char file[100];
	sprintf(file, "/dev/net/%s", my_req.domain);

	fetch_options.out = stdout;
	if (fetch_options.output_file) {
		fetch_options.out = fopen(fetch_options.output_file, "w");
	}

	FILE * f = fopen(file,"r+");

	if (!f) {
		fprintf(stderr, "Nope.\n");
		return 1;
	}

	if (fetch_options.cookie) {
		fprintf(f,
			"GET /%s HTTP/1.0\r\n"
			"User-Agent: curl/7.35.0\r\n"
			"Host: %s\r\n"
			"Accept: */*\r\n"
			"Cookie: %s\r\n"
			"\r\n", my_req.path, my_req.domain, fetch_options.cookie);

	} else {
		fprintf(f,
			"GET /%s HTTP/1.0\r\n"
			"User-Agent: curl/7.35.0\r\n"
			"Host: %s\r\n"
			"Accept: */*\r\n"
			"\r\n", my_req.path, my_req.domain);
	}

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

	fflush(fetch_options.out);

	return 0;
}
