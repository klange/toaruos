/*
 * vim:tabstop=4
 * vim:noexpandtab
 */
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
	asm volatile ("cld; rep movsb" : "+c" (count), "+S" (src), "+D" (dest) :: "memory");
	return dest;
}

int
max(int a, int b) {
	return (a > b) ? a : b;
}

int
abs(int a) {
	return (a >= 0) ? a : -a;
}

void
swap(int *a, int *b) {
	int t = *a;
	*a = *b;
	*b = t;
}

void *
memmove(
		void * restrict dest,
		const void * restrict src,
		size_t count
	  ) {
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
	asm volatile ("cld; rep stosb" : "+c" (count), "+D" (b) : "a" (val) : "memory");
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
uint32_t
strlen(
		const char *str
	  ) {
	int i = 0;
	while (str[i] != (char)0) {
		++i;
	}
	return i;
}

uint32_t __attribute__ ((pure)) krand() {
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

unsigned short
inports(
		unsigned short _port
	   ) {
	unsigned short rv;
	asm volatile ("inw %1, %0" : "=a" (rv) : "dN" (_port));
	return rv;
}

void
outports(
		unsigned short _port,
		unsigned short _data
		) {
	asm volatile ("outw %1, %0" : : "dN" (_port), "a" (_data));
}

unsigned int
inportl(
		unsigned short _port
	   ) {
	unsigned short rv;
	asm volatile ("inl %%dx, %%eax" : "=a" (rv) : "dN" (_port));
	return rv;
}

void
outportl(
		unsigned short _port,
		unsigned int _data
		) {
	asm volatile ("outl %%eax, %%dx" : : "dN" (_port), "a" (_data));
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
	asm volatile ("inb %1, %0" : "=a" (rv) : "dN" (_port));
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
	asm volatile ("outb %1, %0" : : "dN" (_port), "a" (_data));
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
rfind(
		const char * str,
		const char accept
	 ) {
	size_t i = strlen(str) - 1;
	while (str[i] != accept) {
		if (i == 0) return UINT32_MAX;
		i--;
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
