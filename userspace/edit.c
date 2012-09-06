/*
 * edit
 *
 * A super-simple one-pass file... uh "editor".
 * Takes stdin until a blank line and writes
 * it back to standard out.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "lib/utf8decode.h"

#define BLOCK_SIZE 256

uint16_t * file_buffer;
size_t file_len_unicode;
size_t file_len_available;


uint32_t codepoint;
uint32_t state = 0;

int to_eight(uint16_t codepoint, uint8_t * out) {
	memset(out, 0x00, 4);

	if (codepoint < 0x0080) {
		out[0] = (uint8_t)codepoint;
	} else if (codepoint < 0x0800) {
		out[0] = 0xC0 | (codepoint >> 6);
		out[1] = 0x80 | (codepoint & 0x3F);
	} else {
		out[0] = 0xE0 | (codepoint >> 12);
		out[1] = 0x80 | ((codepoint >> 6) & 0x3F);
		out[2] = 0x80 | (codepoint & 0x3F);
	}

	return strlen(out);
}

void add_buffer(uint8_t * buf, int size) {
	for (int i = 0; i < size; ++i) {
		if (!decode(&state, &codepoint, buf[i])) {
			if (file_len_unicode == file_len_available) {
				file_len_available *= 2;
				file_buffer = realloc(file_buffer, file_len_available);
			}
			file_buffer[file_len_unicode] = (uint16_t)codepoint;
			file_len_unicode += 1;
		} else if (state == UTF8_REJECT) {
			state = 0;
		}
	}
}

int main(int argc, char * argv[]) {
	if (argc < 2) {
		fprintf(stderr, "%s: argument expected\n", argv[0]);
		return 1;
	}

	FILE * f = fopen(argv[1], "r");

	if (!f) {
		fprintf(stderr, "%s: Could not open %s\n", argv[0], argv[1]);
		return 1;
	}

	size_t length;

	fseek(f, 0, SEEK_END);
	length = ftell(f);
	fseek(f, 0, SEEK_SET);

	fprintf(stderr, "File is %d bytes long.\n", length);

	file_len_unicode = 0;
	file_len_available = 1024;
	file_buffer = malloc(file_len_available);

	uint8_t buf[BLOCK_SIZE];

	while (length > BLOCK_SIZE) {
		fread(buf, 1, BLOCK_SIZE, f);
		add_buffer(buf, BLOCK_SIZE);
		length -= BLOCK_SIZE;
	}
	if (length > 0) {
		fread(buf, 1, length, f);
		add_buffer((uint8_t *)buf, length);
	}

	fprintf(stderr, "File is %d Unicode characters long.\n", file_len_unicode);

	fclose(f);

	fprintf(stderr, "Writing out file again:\n\n");

	for (int i = 0; i < file_len_unicode; ++i) {
		uint8_t buf[4];
		int len = to_eight(file_buffer[i], buf);
		fwrite(buf, len, 1, stdout);
	}
	fflush(stdout);


	return 0;
}
