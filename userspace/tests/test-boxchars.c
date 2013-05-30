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
