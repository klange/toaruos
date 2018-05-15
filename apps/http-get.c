/* vim: ts=4 sw=4 noexpandtab
 *
 * Small HTTP fetch tool. Not meant to be complete, this is
 * written for demo purposes only.
 */
#include <stdio.h>
#include <unistd.h>

#include <toaru/list.h>
#include <toaru/hashmap.h>

#define SIZE 512

struct uri {
	char domain[SIZE];
	char path[SIZE];
};

void parse_url(char * d, struct uri * r) {
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

int main(int argc, char * argv[]) {

	if (argc < 2) {
		fprintf(stderr, "%s: expected an argument\n", argv[0]);
		return 1;
	}

	struct uri uri;
	parse_url(argv[1], &uri);

	char file[100];
	sprintf(file, "/dev/net/%s", uri.domain);

	FILE * f = fopen(file,"r+");

	if (!f) {
		fprintf(stderr, "%s: connection to %s failed\n", argv[0], uri.domain);
		return 1;
	}

	fprintf(f,
		"GET /%s HTTP/1.0\r\n"
		"User-Agent: curl/7.35.0\r\n"
		"Host: %s\r\n"
		"Accept: */*\r\n"
		"\r\n", uri.path, uri.domain);

	hashmap_t * headers = hashmap_create(10);

	/* Parse response */
	{
		char buf[256];
		read_http_line(buf, f);
		fprintf(stderr, "[%s]\n", buf);

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
			fprintf(stderr, "(done with headers)\n");
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

	fprintf(stderr, "Dumping headers.\n");
	list_t * hash_keys = hashmap_keys(headers);
	foreach(_key, hash_keys) {
		char * key = (char *)_key->value;
		fprintf(stderr, "[%s] = %s\n", key, (char*)hashmap_get(headers, key));
	}
	list_free(hash_keys);
	free(hash_keys);

	/* determine how many bytes we should read now */
	if (!hashmap_has(headers, "Content-Length")) {
		fprintf(stderr, "Don't know how much to read.\n");
		return 1;
	}

	int bytes_to_read = atoi(hashmap_get(headers, "Content-Length"));

	while (bytes_to_read > 0) {
		char buf[1024];
		size_t r = fread(buf, 1, bytes_to_read < 1024 ? bytes_to_read : 1024, f);
		fwrite(buf, 1, r, stdout);
		bytes_to_read -= r;
	}

	return 0;
}
