#include "util.h"

int strcmp(const char * l, const char * r) {
	for (; *l == *r && *l; l++, r++);
	return *(unsigned char *)l - *(unsigned char *)r;
}

char * strchr(const char * s, int c) {
	while (*s) {
		if (*s == c) {
			return (char *)s;
		}
		s++;
	}
	return 0;
}

unsigned long strlen(const char *s) {
	unsigned long out = 0;
	while (*s) {
		out++;
		s++;
	}
	return out;
}

char * strcat(char *dest, const char *src) {
	char * end = dest;
	while (*end != '\0') {
		++end;
	}
	while (*src) {
		*end = *src;
		end++;
		src++;
	}
	*end = '\0';
	return dest;
}

void copy_sectors(unsigned long lba, unsigned char * buf, int sectors) {
	memcpy(buf, (char*)(lba * 2048 + DATA_LOAD_BASE), sectors * 2048);
}

void copy_sector(unsigned long lba, unsigned char * buf) {
	memcpy(buf, (char*)(lba * 2048 + DATA_LOAD_BASE), 2048);
}

