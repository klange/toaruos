#include <wchar.h>

int wcscmp(const wchar_t *l, const wchar_t *r) {
	for (; *l == *r && *l; l++, r++);
	return *(unsigned int *)l - *(unsigned int *)r;
}
