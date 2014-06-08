/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2011-2013 Kevin Lange
 *
 * System Functions
 *
 */
#include <system.h>

unsigned int __irq_sem = 0;

void spin_lock(uint8_t volatile * lock) {
	while(__sync_lock_test_and_set(lock, 0x01)) {
		switch_task(1);
	}
}

void spin_unlock(uint8_t volatile * lock) {
	__sync_lock_release(lock);
}

char * boot_arg = NULL;
char * boot_arg_extra = NULL;

/*
 * memcpy
 * Copy from source to destination. Assumes that
 * source and destination are not overlapping.
 */
void * memcpy(void * restrict dest, const void * restrict src, size_t count) {
	asm volatile ("cld; rep movsb" : "+c" (count), "+S" (src), "+D" (dest) :: "memory");
	return dest;
}

int max(int a, int b) {
	return (a > b) ? a : b;
}

int min(int a, int b) {
	return (a > b) ? b : a;
}

int abs(int a) {
	return (a >= 0) ? a : -a;
}

void swap(int *a, int *b) {
	int t = *a;
	*a = *b;
	*b = t;
}

void * memmove(void * restrict dest, const void * restrict src, size_t count) {
	size_t i;
	unsigned char *a = dest;
	const unsigned char *b = src;
	if (src < dest) {
		for ( i = count; i > 0; --i) {
			a[i-1] = b[i-1];
		}
	} else {
		for ( i = 0; i < count; ++i) {
			a[i] = b[i];
		}
	}
	return dest;
}

int strcmp(const char * a, const char * b) {
	uint32_t i = 0;
	while (1) {
		if (a[i] < b[i]) {
			return -1;
		} else if (a[i] > b[i]) {
			return 1;
		} else {
			if (a[i] == '\0') {
				return 0;
			}
			++i;
		}
	}
}

/*
 * memset
 * Set `count` bytes to `val`.
 */
void * memset(void * b, int val, size_t count) {
	asm volatile ("cld; rep stosb" : "+c" (count), "+D" (b) : "a" (val) : "memory");
	return b;
}

/*
 * memsetw
 * Set `count` shorts to `val`.
 */
unsigned short * memsetw(unsigned short * dest, unsigned short val, int count) {
	int i = 0;
	for ( ; i < count; ++i ) {
		dest[i] = val;
	}
	return dest;
}

/*
 * strlen
 * Returns the length of a given `str`.
 */
uint32_t strlen(const char *str) {
	int i = 0;
	while (str[i] != (char)0) {
		++i;
	}
	return i;
}

char * strdup(const char *str) {
	int len = strlen(str);
	char * out = malloc(sizeof(char) * (len+1));
	memcpy(out, str, len+1);
	return out;
}

char * strcpy(char * dest, const char * src) {
	int len = strlen(src);
	memcpy(dest, src, len+1);
	return dest;
}

uint32_t __attribute__ ((pure)) krand(void) {
	static uint32_t x = 123456789;
	static uint32_t y = 362436069;
	static uint32_t z = 521288629;
	static uint32_t w = 88675123;

	uint32_t t;

	t = x ^ (x << 11);
	x = y; y = z; z = w;
	return w = w ^ (w >> 19) ^ t ^ (t >> 8);
}

/*
 * atoi
 * Naïve implementation thereof.
 */
int atoi(const char * str) {
	uint32_t len = strlen(str);
	uint32_t out = 0;
	uint32_t i;
	uint32_t pow = 1;
	for (i = len; i > 0; --i) {
		out += (str[i-1] - 48) * pow;
		pow *= 10;
	}
	return out;
}

unsigned short inports(unsigned short _port) {
	unsigned short rv;
	asm volatile ("inw %1, %0" : "=a" (rv) : "dN" (_port));
	return rv;
}

void outports(unsigned short _port, unsigned short _data) {
	asm volatile ("outw %1, %0" : : "dN" (_port), "a" (_data));
}

unsigned int inportl(unsigned short _port) {
	unsigned int rv;
	asm volatile ("inl %%dx, %%eax" : "=a" (rv) : "dN" (_port));
	return rv;
}

void outportl(unsigned short _port, unsigned int _data) {
	asm volatile ("outl %%eax, %%dx" : : "dN" (_port), "a" (_data));
}

/*
 * inportb
 * Read from an I/O port.
 */
unsigned char inportb(unsigned short _port) {
	unsigned char rv;
	asm volatile ("inb %1, %0" : "=a" (rv) : "dN" (_port));
	return rv;
}

/*
 * outportb
 * Write to an I/O port.
 */
void outportb(unsigned short _port, unsigned char _data) {
	asm volatile ("outb %1, %0" : : "dN" (_port), "a" (_data));
}

/*
 * Output multiple sets of shorts
 */
void outportsm(unsigned short port, unsigned char * data, unsigned long size) {
	asm volatile ("rep outsw" : "+S" (data), "+c" (size) : "d" (port));
}

/*
 * Input multiple sets of shorts
 */
void inportsm(unsigned short port, unsigned char * data, unsigned long size) {
	asm volatile ("rep insw" : "+D" (data), "+c" (size) : "d" (port) : "memory");
}

char * strtok_r(char * str, const char * delim, char ** saveptr) {
	char * token;
	if (str == NULL) {
		str = *saveptr;
	}
	str += strspn(str, delim);
	if (*str == '\0') {
		*saveptr = str;
		return NULL;
	}
	token = str;
	str = strpbrk(token, delim);
	if (str == NULL) {
		*saveptr = (char *)lfind(token, '\0');
	} else {
		*str = '\0';
		*saveptr = str + 1;
	}
	return token;
}

size_t lfind(const char * str, const char accept) {
	size_t i = 0;
	while ( str[i] != accept) {
		i++;
	}
	return (size_t)(str) + i;
}

size_t rfind(const char * str, const char accept) {
	size_t i = strlen(str) - 1;
	while (str[i] != accept) {
		if (i == 0) return UINT32_MAX;
		i--;
	}
	return (size_t)(str) + i;
}

char * strstr(const char * haystack, const char * needle) {
	const char * out = NULL;
	const char * ptr;
	const char * acc;
	const char * p;
	size_t s = strlen(needle);
	for (ptr = haystack; *ptr != '\0'; ++ptr) {
		size_t accept = 0;
		out = ptr;
		p = ptr;
		for (acc = needle; (*acc != '\0') && (*p != '\0'); ++acc) {
			if (*p == *acc) {
				accept++;
				p++;
			} else {
				break;
			}
		}
		if (accept == s) {
			return (char *)out;
		}
	}
	return NULL;
}

uint8_t startswith(const char * str, const char * accept) {
	size_t s = strlen(accept);
	for (size_t i = 0; i < s; ++i) {
		if (*str != *accept) return 0;
		str++;
		accept++;
	}
	return 1;
}

size_t strspn(const char * str, const char * accept) {
	const char * ptr = str;
	const char * acc;

	while (*str) {
		for (acc = accept; *acc; ++acc) {
			if (*str == *acc) {
				break;
			}
		}
		if (*acc == '\0') {
			break;
		}

		str++;
	}

	return str - ptr;
}

char * strpbrk(const char * str, const char * accept) {
	const char *acc = accept;

	if (!*str) {
		return NULL;
	}

	while (*str) {
		for (acc = accept; *acc; ++acc) {
			if (*str == *acc) {
				break;
			}
		}
		if (*acc) {
			break;
		}
		++str;
	}

	if (*acc == '\0') {
		return NULL;
	}

	return (char *)str;
}

