#include <system.h>

/*
 * memcpy
 * Copy from source to destination. Assumes that
 * source and destination are not overlapping.
 */
void *
memcpy(
		void * restrict dest,
		const void * restrict src,
		size_t count
	  ) {
	size_t i;
	unsigned char *a = dest;
	const unsigned char *b = src;
	for ( i = 0; i < count; ++i ) {
		a[i] = b[i];
	}
	return dest;
}

int
strcmp(
		const char * a,
		const char * b
	  ) {
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
void *
memset(
		void *b,
		int val,
		size_t count
	  ) {
	size_t i;
	unsigned char * dest = b;
	for ( i = 0; i < count; ++i ) {
		dest[i] = (unsigned char)val;
	}
	return b;
}

/*
 * memsetw
 * Set `count` shorts to `val`.
 */
unsigned short *
memsetw(
		unsigned short *dest,
		unsigned short val,
		int count
	) {
	int i;
	i = 0;
	for ( ; i < count; ++i ) {
		dest[i] = val;
	}
	return dest;
}

/*
 * strlen
 * Returns the length of a given `str`.
 */
int
strlen(
		const char *str
	  ) {
	int i = 0;
	while (str[i] != (char)0) {
		++i;
	}
	return i;
}

/*
 * atoi
 * Naïve implementation thereof.
 */
int
atoi(
		const char *str
	) {
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

/*
 * inportb
 * Read from an I/O port.
 */
unsigned char
inportb(
		unsigned short _port
	   ) {
	unsigned char rv;
	__asm__ __volatile__ ("inb %1, %0" : "=a" (rv) : "dN" (_port));
	return rv;
}

/*
 * outportb
 * Write to an I/O port.
 */
void
outportb(
		unsigned short _port,
		unsigned char _data
		) {
	__asm__ __volatile__ ("outb %1, %0" : : "dN" (_port), "a" (_data));
}

char *
strtok_r(
		char * str,
		const char * delim,
		char ** saveptr
		) {
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

size_t
lfind(
		const char * str,
		const char accept
	 ) {
	size_t i = 0;
	while ( str[i] != accept) {
		i++;
	}
	return (size_t)(str) + i;
}

size_t
strspn(
		const char * str,
		const char * accept
	  ) {
	const char * ptr;
	const char * acc;
	size_t size = 0;
	for (ptr = str; *ptr != '\0'; ++ptr) {
		for (acc = accept; *acc != '\0'; ++acc) {
			if (*ptr == *acc) {
				break;
			}
		}
		if (*acc == '\0') {
			return size;
		} else {
			++size;
		}
	}
	return size;
}

char *
strpbrk(
		const char * str,
		const char * accept
	   ) {
	while (*str != '\0') {
		const char *acc = accept;
		while (*acc != '\0') {
			if (*acc++ == *str) {
				return (char *) str;
			}
		}
		++str;
	}
	return NULL;
}
