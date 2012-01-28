/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * yes
 */
#include <stdio.h>

int main(int argc, char * argv[]) {
	char * yes_string = "y";
	if (argc > 1) {
		yes_string = argv[1];
	}
	while (1) {
		printf("%s\n", yes_string);
	}
	return 0;
}
