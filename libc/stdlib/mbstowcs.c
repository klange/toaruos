#include <stdlib.h>
#include <wchar.h>
#include <string.h>
#include <stdio.h>

#include <toaru/decodeutf8.h>

static int to_eight(uint32_t codepoint, char * out) {
	memset(out, 0x00, 7);

	if (codepoint < 0x0080) {
		out[0] = (char)codepoint;
	} else if (codepoint < 0x0800) {
		out[0] = 0xC0 | (codepoint >> 6);
		out[1] = 0x80 | (codepoint & 0x3F);
	} else if (codepoint < 0x10000) {
		out[0] = 0xE0 | (codepoint >> 12);
		out[1] = 0x80 | ((codepoint >> 6) & 0x3F);
		out[2] = 0x80 | (codepoint & 0x3F);
	} else if (codepoint < 0x200000) {
		out[0] = 0xF0 | (codepoint >> 18);
		out[1] = 0x80 | ((codepoint >> 12) & 0x3F);
		out[2] = 0x80 | ((codepoint >> 6) & 0x3F);
		out[3] = 0x80 | ((codepoint) & 0x3F);
	} else if (codepoint < 0x4000000) {
		out[0] = 0xF8 | (codepoint >> 24);
		out[1] = 0x80 | (codepoint >> 18);
		out[2] = 0x80 | ((codepoint >> 12) & 0x3F);
		out[3] = 0x80 | ((codepoint >> 6) & 0x3F);
		out[4] = 0x80 | ((codepoint) & 0x3F);
	} else {
		out[0] = 0xF8 | (codepoint >> 30);
		out[1] = 0x80 | ((codepoint >> 24) & 0x3F);
		out[2] = 0x80 | ((codepoint >> 18) & 0x3F);
		out[3] = 0x80 | ((codepoint >> 12) & 0x3F);
		out[4] = 0x80 | ((codepoint >> 6) & 0x3F);
		out[5] = 0x80 | ((codepoint) & 0x3F);
	}

	return strlen(out);
}

size_t mbstowcs(wchar_t *dest, const char *src, size_t n) {
	size_t count = 0;
	uint32_t state = 0;
	uint32_t codepoint = 0;

	while ((!dest || count < n) && *src) {
		if (!decode(&state, &codepoint, *(unsigned char *)src)) {
			if (dest) {
				dest[count] = codepoint;
			}
			count++;
			codepoint = 0;
		} else if (state == UTF8_REJECT) {
			return (size_t)-1;
		}
		src++;
	}

	if (dest && !*src && count < n) {
		dest[count] = L'\0';
	}

	return count;
}

size_t wcstombs(char * dest, const wchar_t *src, size_t n) {
	size_t count = 0;

	while ((!dest || count < n) && *src) {
		char tmp[7];
		int size = to_eight(*src, tmp);
		if (count + size > n) return n;
		memcpy(&dest[count], tmp, size);
		count += size;
		src++;
	}

	if (dest && !*src && count < n) {
		dest[count] = '\0';
	}

	return count;
}

