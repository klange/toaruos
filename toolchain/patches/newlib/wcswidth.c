/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2014 Kevin Lange
 */
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
