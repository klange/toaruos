#include <wchar.h>

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
