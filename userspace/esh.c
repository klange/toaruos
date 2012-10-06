/*
 * E-Shell
 *
 * Test shell for ToAruOS
 */

#include <stdio.h>
#include <stdint.h>
#include <syscall.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

//DEFN_SYSCALL1(wait, 17, unsigned int);

int main(int argc, char ** argv) {
	printf("I am pid %d\n", getpid());
	char cwd[1024] = {'/',0};

	int  pid = getpid();
	int  nowait = 0;
	int  free_cmd = 0;
	while (1) {
		char * cmd = malloc(sizeof(char) * 1024);
		printf("%s$ ", cwd);
		fflush(stdout);
		fgets(cmd, 1024, stdin);
		cmd[strlen(cmd)-1] = '\0';
		char *p, *tokens[512], *last;
		int i = 0;
		for ((p = strtok_r(cmd, " ", &last)); p;
				(p = strtok_r(NULL, " ", &last)), i++) {
			if (i < 511) tokens[i] = p;
		}
		tokens[i] = NULL;
		if (!tokens[0] || strlen(tokens[0]) < 1) {
			free(cmd);
			continue;
		}
		if (!strcmp(tokens[0],"exit")) {
			goto exit;
		}
		nowait = (!strcmp(tokens[i-1],"&"));

		/* Attempt to open the command */
		FILE * file = fopen(tokens[0], "r");
		if (!file) {
			if (!strstr(tokens[0],"/")) {
				cmd = malloc(sizeof(char) * (strlen(tokens[0]) + strlen("/bin/") + 1));
				sprintf(cmd, "%s%s", "/bin/", tokens[0]);
				file = fopen(cmd,"r");
				if (!file) {
					printf("Command not found: %s\n", tokens[0]);
					free(cmd);
					continue;
				}
				fclose(file);
			} else {
				printf("Command not found: %s\n", tokens[0]);
				free(cmd);
				continue;
			}
		} else {
			fclose(file);
		}


		uint32_t f = fork();
		if (getpid() != pid) {
			int i = execve(cmd, tokens, NULL);
			return i;
		} else {
			if (!nowait) {
				int i = syscall_wait(f);
				if (i) {
					printf("[%d] ");
				}
			}
			free(cmd);
		}
	}
exit:

	return 0;
}
