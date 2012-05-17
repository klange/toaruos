/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Who Am I?
 *
 */
#include <stdio.h>
#include <syscall.h>
#include <string.h>
#include <stdlib.h>

#define LINE_LEN 4096

int main(int argc, char ** argv) {
	FILE * passwd = fopen("/etc/passwd", "r");
	char line[LINE_LEN];
	
	int uid = syscall_getuid();

	while (fgets(line, LINE_LEN, passwd) != NULL) {

		line[strlen(line)-1] = '\0';

		char *p, *tokens[10], *last;
		int i = 0;
		for ((p = strtok_r(line, ":", &last)); p;
				(p = strtok_r(NULL, ":", &last)), i++) {
			if (i < 511) tokens[i] = p;
		}
		tokens[i] = NULL;

		if (atoi(tokens[2]) == uid) {
			printf("%s\n", tokens[0]);
		}
	}
	fclose(passwd);

	return 0;
}

