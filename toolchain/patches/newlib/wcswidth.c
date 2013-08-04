#include <wchar.h>
int wcswidth(const wchar_t * pwcs, size_t n) {
	int width = 0;
	for (; *pwcs && n-- > 0; ++pwcs) {
		int w = wcwidth(*pwcs);
		if (w < 0) {
			return -1;
		} else {
			width += w;
		}
	}
	return width;
}
