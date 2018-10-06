/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2015-2018 K. Lange
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

#include <toaru/hashmap.h>

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
	size_t content_length;
	size_t size;
	struct timeval start;
	int calculate_output;
	int slow_upload;
	int machine_readable;
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
	fprintf(stderr,"\033[G%6dkB",(int)fetch_options.size/1024);
	if (fetch_options.content_length) {
		int percent = (fetch_options.size * BAR_WIDTH) / (fetch_options.content_length);
		fprintf(stderr," / %6dkB [%.*s%.*s]", (int)fetch_options.content_length/1024, percent,bar_perc,BAR_WIDTH-percent,bar_spac);
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

int usage(char * argv[]) {
	fprintf(stderr,
			"fetch - download files over HTTP\n"
			"\n"
			"usage: %s [-hOvmp?] [-c cookie] [-o file] [-u file] [-s speed] URL\n"
			"\n"
			" -h     \033[3mshow headers\033[0m\n"
			" -O     \033[3msave the file based on the filename in the URL\033[0m\n"
			" -v     \033[3mshow progress\033[0m\n"
			" -m     \033[3mmachine readable output\033[0m\n"
			" -p     \033[3mprompt for password\033[0m\n"
			" -c ... \033[3mset cookies\033[0m\n"
			" -o ... \033[3msave to the specified file\033[0m\n"
			" -u ... \033[3mupload the specified file\033[0m\n"
			" -s ... \033[3mspecify the speed for uploading slowly\033[0m\n"
			" -?     \033[3mshow this help text\033[0m\n"
			"\n", argv[0]);
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

	return 0;
}

void read_http_line(char * buf, FILE * f) {
	memset(buf, 0x00, 256);

	fgets(buf, 255, f);
	char * _r = strchr(buf, '\r');
	if (_r) {
		*_r = '\0';
	}
	if (!_r) {
		_r = strchr(buf, '\n'); /* that's not right, but, whatever */
		if (_r) {
			*_r = '\0';
		}
	}
}

void bad_response(void) {
	fprintf(stderr, "Bad response.\n");
	exit(1);
}

int http_fetch(FILE * f) {
	hashmap_t * headers = hashmap_create(10);

	/* Parse response */
	{
		char buf[256];
		read_http_line(buf, f);

		char * elements[3];

		elements[0] = buf;
		elements[1] = strchr(elements[0], ' ');
		if (!elements[1]) bad_response();
		*elements[1] = '\0';
		elements[1]++;

		elements[2] = strchr(elements[1], ' ');
		if (!elements[2]) bad_response();
		*elements[2] = '\0';
		elements[2]++;

		if (strcmp(elements[1], "200")) {
			fprintf(stderr, "Bad response code: %s\n", elements[1]);
			return 1;
		}
	}

	/* Parse headers */
	while (1) {
		char buf[256];
		read_http_line(buf, f);

		if (!*buf) {
			break;
		}

		/* Split */
		char * name = buf;
		char * value = strstr(buf, ": ");
		if (!value) bad_response();
		*value = '\0';
		value += 2;

		hashmap_set(headers, name, strdup(value));
	}

	if (fetch_options.show_headers) {
		list_t * hash_keys = hashmap_keys(headers);
		foreach(_key, hash_keys) {
			char * key = (char *)_key->value;
			fprintf(stderr, "[%s] = %s\n", key, (char*)hashmap_get(headers, key));
		}
		list_free(hash_keys);
		free(hash_keys);
	}

	/* determine how many bytes we should read now */
	if (!hashmap_has(headers, "Content-Length")) {
		fprintf(stderr, "Don't know how much to read.\n");
		return 1;
	}

	int bytes_to_read = atoi(hashmap_get(headers, "Content-Length"));
	fetch_options.content_length = bytes_to_read;

	while (bytes_to_read > 0) {
		char buf[1024];
		size_t r = fread(buf, 1, bytes_to_read < 1024 ? bytes_to_read : 1024, f);
		fwrite(buf, 1, r, fetch_options.out);
		fetch_options.size += r;
		if (fetch_options.show_progress) {
			print_progress();
		}
		if (fetch_options.machine_readable && fetch_options.content_length) {
			fprintf(stdout,"%d %d\n",(int)fetch_options.size, (int)fetch_options.content_length);
		}
		bytes_to_read -= r;
	}

	return 0;
}

int main(int argc, char * argv[]) {

	int opt;

	while ((opt = getopt(argc, argv, "?c:hmo:Opu:vs:")) != -1) {
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
			case 'm':
				fetch_options.machine_readable = 1;
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
		fetch_options.out = fopen(fetch_options.output_file, "w+");
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
			"\r\n", my_req.path, my_req.domain, (int)out_size, boundary_fuzz);

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

	http_fetch(f);

	fflush(fetch_options.out);

	if (fetch_options.show_progress) {
		fprintf(stderr,"\n");
	}

	if (fetch_options.machine_readable) {
		fprintf(stdout,"done\n");
	}

	return 0;
}
