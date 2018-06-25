#include <wchar.h>

size_t wcslen(const wchar_t * s) {
	size_t out = 0;
	while (*s) {
		out++;
		s++;
	}
	return out;
}
