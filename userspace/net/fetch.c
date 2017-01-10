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
#include <time.h>
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>

#include "lib/http_parser.h"

#define SIZE 512
#define BOUNDARY "------ToaruOSFetchUploadBoundary"

struct http_req {
	char domain[SIZE];
	char path[SIZE];
};

struct {
	int show_headers;
	const char * output_file;
	const char * cookie;
	FILE * out;
	int prompt_password;
	const char * upload_file;
	char * password;
	int show_progress;
	int next_is_content_length;
	size_t content_length;
	size_t size;
	struct timeval start;
	int calculate_output;
	int slow_upload;
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

#define BAR_WIDTH 20
#define bar_perc "||||||||||||||||||||"
#define bar_spac "                    "
void print_progress(void) {
	struct timeval now;
	gettimeofday(&now, NULL);
	fprintf(stderr,"\033[G%6dkB",fetch_options.size/1024);
	if (fetch_options.content_length) {
		int percent = (fetch_options.size * BAR_WIDTH) / (fetch_options.content_length);
		fprintf(stderr," / %6dkB [%.*s%.*s]", fetch_options.content_length/1024, percent,bar_perc,BAR_WIDTH-percent,bar_spac);
	}

	double timediff = (double)(now.tv_sec - fetch_options.start.tv_sec) + (double)(now.tv_usec - fetch_options.start.tv_usec)/1000000.0;
	if (timediff > 0.0) {
		double rate = (double)(fetch_options.size) / timediff;
		double s = rate/(1024.0) * 8.0;
		if (s > 1024.0) {
			fprintf(stderr," %.2f mbps", s/1024.0);
		} else {
			fprintf(stderr," %.2f kbps", s);
		}

		if (fetch_options.content_length) {
			if (rate > 0.0) {
				double remaining = (double)(fetch_options.content_length - fetch_options.size) / rate;

				fprintf(stderr," (%.2f sec remaining)", remaining);
			}
		}
	}
	fprintf(stderr,"\033[K");
	fflush(stderr);
}

int callback_header_field (http_parser *p, const char *buf, size_t len) {
	if (fetch_options.show_headers) {
		fprintf(stderr, "Header field: %.*s\n", len, buf);
	}
	if (!strncmp(buf,"Content-Length",len)) {
		fetch_options.next_is_content_length = 1;
	} else {
		fetch_options.next_is_content_length = 0;
	}
	return 0;
}

int callback_header_value (http_parser *p, const char *buf, size_t len) {
	if (fetch_options.show_headers) {
		fprintf(stderr, "Header value: %.*s\n", len, buf);
	}
	if (fetch_options.next_is_content_length) {
		char tmp[len+1];
		memcpy(tmp,buf,len);
		tmp[len] = '\0';
		fetch_options.content_length = atoi(tmp);
	}
	return 0;
}

int callback_body (http_parser *p, const char *buf, size_t len) {
	fwrite(buf, 1, len, fetch_options.out);
	fetch_options.size += len;
	if (fetch_options.show_progress) {
		print_progress();
	}
	return 0;
}

int usage(char * argv[]) {
	fprintf(stderr, "Usage: %s [-h] [-c cookie] [-o file] url\n", argv[0]);
	return 1;
}

int collect_password(char * password) {
	fprintf(stdout, "Password for upload: ");
	fflush(stdout);

	/* Disable echo */
	struct termios old, new;
	tcgetattr(fileno(stdin), &old);
	new = old;
	new.c_lflag &= (~ECHO);
	tcsetattr(fileno(stdin), TCSAFLUSH, &new);

	fgets(password, 1024, stdin);
	password[strlen(password)-1] = '\0';
	tcsetattr(fileno(stdin), TCSAFLUSH, &old);
	fprintf(stdout, "\n");
}

int main(int argc, char * argv[]) {

	int opt;

	while ((opt = getopt(argc, argv, "?c:ho:Opu:vs:")) != -1) {
		switch (opt) {
			case '?':
				return usage(argv);
			case 'O':
				fetch_options.calculate_output = 1;
				break;
			case 'c':
				fetch_options.cookie = optarg;
				break;
			case 'h':
				fetch_options.show_headers = 1;
				break;
			case 'o':
				fetch_options.output_file = optarg;
				break;
			case 'u':
				fetch_options.upload_file = optarg;
				break;
			case 'v':
				fetch_options.show_progress = 1;
				break;
			case 'p':
				fetch_options.prompt_password = 1;
				break;
			case 's':
				fetch_options.slow_upload = atoi(optarg);
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

	if (fetch_options.calculate_output) {
		char * tmp = strdup(my_req.path);
		char * x = strrchr(tmp,'/');
		if (x) {
			tmp = x + 1;
		}
		fetch_options.output_file = tmp;
	}

	fetch_options.out = stdout;
	if (fetch_options.output_file) {
		fetch_options.out = fopen(fetch_options.output_file, "w");
	}

	FILE * f = fopen(file,"r+");

	if (!f) {
		fprintf(stderr, "Nope.\n");
		return 1;
	}

	if (fetch_options.prompt_password) {
		fetch_options.password = malloc(100);
		collect_password(fetch_options.password);
	}

	if (fetch_options.upload_file) {
		FILE * in_file = fopen(fetch_options.upload_file, "r");

		srand(time(NULL));
		int boundary_fuzz = rand();
		char tmp[512];

		size_t out_size = 0;
		if (fetch_options.password) {
			out_size += sprintf(tmp,
				"--" BOUNDARY "%08x\r\n"
				"Content-Disposition: form-data; name=\"password\"\r\n"
				"\r\n"
				"%s\r\n",boundary_fuzz, fetch_options.password);
		}

		out_size += strlen("--" BOUNDARY "00000000\r\n"
				"Content-Disposition: form-data; name=\"file\"; filename=\"\"\r\n"
				"Content-Type: application/octet-stream\r\n"
				"\r\n"
				/* Data goes here */
				"\r\n"
				"--" BOUNDARY "00000000" "--\r\n");

		out_size += strlen(fetch_options.upload_file);

		fseek(in_file, 0, SEEK_END);
		out_size += ftell(in_file);
		fseek(in_file, 0, SEEK_SET);

		fprintf(f,
			"POST /%s HTTP/1.0\r\n"
			"User-Agent: curl/7.35.0\r\n"
			"Host: %s\r\n"
			"Accept: */*\r\n"
			"Content-Length: %d\r\n"
			"Content-Type: multipart/form-data; boundary=" BOUNDARY "%08x\r\n"
			"\r\n", my_req.path, my_req.domain, out_size, boundary_fuzz);

		fprintf(f,"%s",tmp);
		fprintf(f,
				"--" BOUNDARY "%08x\r\n"
				"Content-Disposition: form-data; name=\"file\"; filename=\"%s\"\r\n"
				"Content-Type: application/octet-stream\r\n"
				"\r\n", boundary_fuzz, fetch_options.upload_file);

		while (!feof(in_file)) {
			char buf[1024];
			size_t r = fread(buf, 1, 1024, in_file);
			fwrite(buf, 1, r, f);
			if (fetch_options.slow_upload) {
				usleep(1000 * fetch_options.slow_upload); /* TODO fix terrible network stack; hopefully this ensures we send stuff right. */
			}
		}

		fclose(in_file);

		fprintf(f,"\r\n--" BOUNDARY "%08x--\r\n", boundary_fuzz);
		fflush(f);

	} else if (fetch_options.cookie) {
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

	gettimeofday(&fetch_options.start, NULL);
	while (!feof(f)) {
		char buf[10240];
		memset(buf, 0, sizeof(buf));
		size_t r = fread(buf, 1, 10240, f);
		http_parser_execute(&parser, &settings, buf, r);
	}

	fflush(fetch_options.out);

	if (fetch_options.show_progress) {
		fprintf(stderr,"\n");
	}

	return 0;
}
