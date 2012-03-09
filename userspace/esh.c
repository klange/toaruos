/* vim: tabstop=4 shiftwidth=4 noexpandtab
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
#include <time.h>

DEFN_SYSCALL2(getcwd, 29, char *, size_t);
DEFN_SYSCALL1(chdir, 28, char *);

DEFN_SYSCALL0(getuid, 23);
DEFN_SYSCALL0(gethostname, 32);

DEFN_SYSCALL2(signal, 38, uint32_t, void *);
DEFN_SYSCALL2(send_signal, 37, uint32_t, uint32_t)

#define LINE_LEN 4096

char cwd[1024] = {'/',0};
struct timeval {
	unsigned int tv_sec;
	unsigned int tv_usec;
};

char username[1024];
char _hostname[256];

void getusername() {
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
			memcpy(username, tokens[0], strlen(tokens[0]) + 1);
		}
	}
	fclose(passwd);
}

void gethostname() {
	char buffer[256];
	size_t len = syscall_gethostname(buffer);
	memcpy(_hostname, buffer, len);
}

void draw_prompt(int ret) {
	struct tm * timeinfo;
	struct timeval now;
	syscall_gettimeofday(&now, NULL); //time(NULL);
	timeinfo = localtime((time_t *)&now.tv_sec);
	char date_buffer[80];
	strftime(date_buffer, 80, "%m/%d", timeinfo);
	char time_buffer[80];
	strftime(time_buffer, 80, "%H:%M:%S", timeinfo);
	printf("\033[1m[\033[1;33m%s \033[1;32m%s \033[1;31m%s \033[1;34m%s\033[0m ",
			username, _hostname, date_buffer, time_buffer);
	if (ret != 0) {
		printf("\033[1;31m%d ", ret);
	}
	syscall_getcwd(cwd, 1024);
	printf("\033[0m%s\033[1m]\033[0m\n\033[1;32m$\033[0m ", cwd);
	fflush(stdout);
}

uint32_t child = 0;

void sig_int(int sig) {
	static uint32_t times = 1;
	if (child) {
		syscall_send_signal(child, sig);
	} else {
		printf("stop that! you've interrupted me %d time%s!\n", times, (times > 1) ? "s" : "");
		times++;
	}
}

int main(int argc, char ** argv) {
	int  pid = getpid();
	int  nowait = 0;
	int  free_cmd = 0;
	int  last_ret = 0;

	syscall_signal(2, sig_int);

	getusername();
	gethostname();

	FILE * motd = fopen("/etc/motd", "r");
	if (motd) {
		size_t s = 0;
		fseek(motd, 0, SEEK_END);
		s = ftell(motd);
		fseek(motd, 0, SEEK_SET);
		char * m = malloc(sizeof(char) * s);
		fread(m, s, 1, motd);
		fwrite(m, s, 1, stdout);
		fprintf(stdout, "\n");
		fflush(stdout);
		free(m);
	}

	while (1) {
		char * cmd = malloc(sizeof(char) * 1024);

		draw_prompt(last_ret);
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
		if (!strcmp(tokens[0],"cd")) {
			if (i > 1) {
				if (syscall_chdir(tokens[1]) != 0) {
					printf("cd: Could not cd to '%s'.\n", tokens[1]);
					last_ret = 1;
				}
			} else {
				printf("cd: expected argument\n");
				last_ret = 1;
			}
			free(cmd);
			continue;
		}
		nowait = (!strcmp(tokens[i-1],"&"));
		if (nowait) {
			tokens[i-1] = NULL;
		}

		/* Attempt to open the command */
		FILE * file = NULL; //fopen(tokens[0], "r");
		if (!strstr(tokens[0],"/")) {
			cmd = malloc(sizeof(char) * (strlen(tokens[0]) + strlen("/bin/") + 1));
			sprintf(cmd, "%s%s", "/bin/", tokens[0]);
			file = fopen(cmd,"r");
			if (!file) {
				printf("Command not found: %s\n", tokens[0]);
				last_ret = 1;
				free(cmd);
				continue;
			}
			fclose(file);
		} else {
			file = fopen(tokens[0], "r");
			if (!file) {
				printf("Command not found: %s\n", tokens[0]);
				last_ret = 1;
				free(cmd);
				continue;
			}
			fclose(file);
		}


		uint32_t f = fork();
		if (getpid() != pid) {
			int i = execve(cmd, tokens, NULL);
			return i;
		} else {
			if (!nowait) {
				child = f;
				last_ret = syscall_wait(f);
				child = 0;
			}
			free(cmd);
		}
	}
exit:

	return 0;
}
