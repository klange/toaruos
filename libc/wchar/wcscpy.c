#include <wchar.h>

wchar_t * wcscpy(wchar_t * restrict dest, const wchar_t * restrict src) {
	wchar_t * out = dest;
	for (; (*dest=*src); src++, dest++);
	return out;
}
