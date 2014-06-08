/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014 Kevin Lange
 */
#include <stdio.h>
#include <string.h>

#include "lib/utf8decode.h"

static char * c = "🍕";
static char * t = "😎";
static char * z = "😸";
static char * y = "😹";

static uint32_t codepoint;
static uint32_t state = 0;

void decodestring(char * s) {
	uint32_t o = 0;
	char * c = s;

	while (*s) {
		if (!decode(&state, &codepoint, (uint8_t)*s)) {
			o = (uint32_t)codepoint;
			s++;
			goto decoded;
		} else if (state == UTF8_REJECT) {
			state = 0;
		}
		s++;
	}
decoded:
	fprintf(stdout, "Decoded %s to 0x%x (%d)\n", c, codepoint, codepoint);
}

int main(int argc, char * argv[]) {
	fprintf(stdout, "Length(:pizza:) = %d\n", strlen(c));

	for (int i = 0; i < 5; ++i) {
		decodestring(c);
		decodestring(t);
		decodestring(z);
		decodestring(y);
	}

	return 0;
}
