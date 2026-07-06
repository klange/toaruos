#include <stdio.h>
#include <sys/utsname.h>
int main(int argc, char * argv[]) {
	if (argc != 1) return fprintf(stderr, "%s: extra operand\n", argv[0]), 1;
	struct utsname u;
	uname(&u);
	puts(u.machine);
}
