/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013 Kevin Lange
 */
#include <stdio.h>
#include <wchar.h>

int main(int argc, char * argv[]) {
	wchar_t * c = L"▒␉␌␍␊°±␤␋┘┐┌└┼⎺⎻─⎼⎽├┤┴┬│≤≥";
	char d = 'a';
	while (*c) {
		printf("%d - %c \033(0%c\033(B\n", *c, d, d);
		c++;
		d++;
	}
	return 0;
}
