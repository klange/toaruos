#include <stdio.h>

int main(int argc, char * argv[]) {
	char buf[L_ctermid];
	char * result = ctermid(buf);
	fprintf(stdout, "%s\n", result);
	return 0;
}
