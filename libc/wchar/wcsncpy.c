#include <wchar.h>

wchar_t * wcsncpy(wchar_t * dest, const wchar_t * src, size_t n) {
	wchar_t * out = dest;
	while (n > 0) {
		*dest = *src;
		if (!*src) break;
		dest++;
		src++;
		n--;
	}
	return out;
}
