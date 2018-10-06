#include <wchar.h>

size_t wcsspn(const wchar_t * wcs, const wchar_t * accept) {
	size_t out = 0;

	while (*wcs) {
		int good = 0;
		for (const wchar_t * a = accept; *a; ++a) {
			if (*wcs == *a) {
				good = 1;
				break;
			}
		}
		if (!good) break;
		out++;
		wcs++;
	}

	return out;
}

wchar_t *wcspbrk(const wchar_t *wcs, const wchar_t *accept) {
	while (*wcs) {
		for (const wchar_t * a = accept; *a; ++a) {
			if (*wcs == *a) {
				return (wchar_t *)wcs;
			}
		}
		wcs++;
	}
	return NULL;
}

wchar_t * wcschr(const wchar_t *wcs, wchar_t wc) {
	while (*wcs != wc && *wcs != 0) {
		wcs++;
	}
	if (!*wcs) return NULL;
	return (wchar_t *)wcs;
}

wchar_t * wcsrchr(const wchar_t *wcs, wchar_t wc) {
	wchar_t * last = NULL;
	while (*wcs != 0) {
		if (*wcs == wc) {
			last = (wchar_t *)wcs;
		}
		wcs++;
	}
	return last;
}

wchar_t * wcstok(wchar_t * str, const wchar_t * delim, wchar_t ** saveptr) {
	wchar_t * token;
	if (str == NULL) {
		str = *saveptr;
	}
	str += wcsspn(str, delim);
	if (*str == '\0') {
		*saveptr = str;
		return NULL;
	}
	token = str;
	str = wcspbrk(token, delim);
	if (str == NULL) {
		*saveptr = (wchar_t *)wcschr(token, '\0');
	} else {
		*str = '\0';
		*saveptr = str + 1;
	}
	return token;
}


