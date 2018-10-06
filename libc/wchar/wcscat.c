#include <wchar.h>
#include <stdio.h>

wchar_t * wcscat(wchar_t *dest, const wchar_t *src) {
	wchar_t * end = dest;
	while (*end != 0) {
		++end;
	}
	while (*src) {
		*end = *src;
		end++;
		src++;
	}
	*end = 0;
	return dest;
}

wchar_t * wcsncat(wchar_t *dest, const wchar_t * src, size_t n) {
	wchar_t * end = dest;
	size_t c = 0;
	while (*end != 0) {
		++end;
	}
	while (*src && c < n) {
		*end = *src;
		end++;
		src++;
		c++;
	}
	*end = 0;
	return dest;
}
