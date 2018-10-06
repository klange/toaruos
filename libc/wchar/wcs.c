#include <wchar.h>

size_t wcstombs(char * dest, const wchar_t *src, size_t n) {
	/* TODO */
	size_t c = 0;
	while (c < n && *src) {
		*dest = *src;
		c++;
		src++;
		dest++;
	}
	*dest = 0;
	return c;
}

size_t mbstowcs(wchar_t * dest, const char *src, size_t n) {
	/* TODO */
	size_t c = 0;
	while (c < n && *src) {
		*dest = *src;
		c++;
		src++;
		dest++;
	}
	*dest = 0;
	return c;
}
