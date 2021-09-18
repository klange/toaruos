#include <wchar.h>

int wcscmp(const wchar_t *l, const wchar_t *r) {
	for (; *l == *r && *l; l++, r++);
	return *(unsigned int *)l - *(unsigned int *)r;
}

int wcsncmp(const wchar_t *s1, const wchar_t *s2, size_t n) {
	if (n == 0) return 0;

	while (n-- && *s1 == *s2) {
		if (!n || !*s1) break;
		s1++;
		s2++;
	}
	return (*s1) - (*s2);
}
