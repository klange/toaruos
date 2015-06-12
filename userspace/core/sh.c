/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2014 Kevin Lange
 *
 * E-Shell
 *
 * This is the "experimental shell". It provides
 * a somewhat unix-like shell environment, but does
 * not include a parser any advanced functionality.
 * It simply cuts its input into arguments and executes
 * programs.
 */

#define _XOPEN_SOURCE

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <dirent.h>
#include <signal.h>
#include <getopt.h>
#include <termios.h>
#include <errno.h>

#include <sys/time.h>
#include <sys/wait.h>
#include <sys/utsname.h>

#include "lib/list.h"
#include "lib/kbd.h"
#include "lib/rline.h"

#define PIPE_TOKEN "\xFF\xFFPIPE\xFF\xFF"

/* A shell command is like a C program */
typedef uint32_t(*shell_command_t) (int argc, char ** argv);

/* We have a static array that fits a certain number of them. */
#define SHELL_COMMANDS 512
char * shell_commands[SHELL_COMMANDS];          /* Command names */
shell_command_t shell_pointers[SHELL_COMMANDS]; /* Command functions */

/* This is the number of actual commands installed */
uint32_t shell_commands_len = 0;

/* We also support history through a circular buffer. */
#define SHELL_HISTORY_ENTRIES 128
char * shell_history[SHELL_HISTORY_ENTRIES];
int shell_history_count  = 0;
int shell_history_offset = 0;
int shell_scroll = 0;
char   shell_temp[1024];

int    shell_interactive = 1;
int    shell_force_raw   = 0;

int pid; /* Process ID of the shell */

char * shell_history_prev(int item);

void shell_history_insert(char * str) {
	if (str[strlen(str)-1] == '\n') {
		str[strlen(str)-1] = '\0';
	}
	if (shell_history_count) {
		if (!strcmp(str, shell_history_prev(1))) {
			free(str);
			return;
		}
	}
	if (shell_history_count == SHELL_HISTORY_ENTRIES) {
		free(shell_history[shell_history_offset]);
		shell_history[shell_history_offset] = str;
		shell_history_offset = (shell_history_offset + 1) % SHELL_HISTORY_ENTRIES;
	} else {
		shell_history[shell_history_count] = str;
		shell_history_count++;
	}
}

void shell_history_append_line(char * str) {
	if (shell_history_count) {
		char ** s = &shell_history[(shell_history_count - 1 + shell_history_offset) % SHELL_HISTORY_ENTRIES];
		char * c = malloc(strlen(*s) + strlen(str) + 2);
		sprintf(c, "%s\n%s", *s, str);
		if (c[strlen(c)-1] == '\n') {
			c[strlen(c)-1] = '\0';
		}
		free(*s);
		*s = c;
	} else {
		/* wat */
	}
}

char * shell_history_get(int item) {
	return shell_history[(item + shell_history_offset) % SHELL_HISTORY_ENTRIES];
}

char * shell_history_prev(int item) {
	return shell_history_get(shell_history_count - item);
}

void shell_install_command(char * name, shell_command_t func) {
	if (shell_commands_len == SHELL_COMMANDS) {
		fprintf(stderr, "Ran out of space for static shell commands. The maximum number of commands is %d\n", SHELL_COMMANDS);
		return;
	}
	shell_commands[shell_commands_len] = name;
	shell_pointers[shell_commands_len] = func;
	shell_commands_len++;
}

shell_command_t shell_find(char * str) {
	for (uint32_t i = 0; i < shell_commands_len; ++i) {
		if (!strcmp(str, shell_commands[i])) {
			return shell_pointers[i];
		}
	}
	return NULL;
}

struct termios old;

void set_unbuffered() {
	tcgetattr(fileno(stdin), &old);
	struct termios new = old;
	new.c_lflag &= (~ICANON & ~ECHO);
	tcsetattr(fileno(stdin), TCSAFLUSH, &new);
}

void set_buffered() {
	tcsetattr(fileno(stdin), TCSAFLUSH, &old);
}

void install_commands();

/* Maximum command length */
#define LINE_LEN 4096

/* Current working directory */
char cwd[1024] = {'/',0};

/* Username */
char username[1024];

/* Hostname for prompt */
char _hostname[256];

/* function to update the cached username */
void getuser() {
	char * tmp = getenv("USER");
	if (tmp) {
		strcpy(username, tmp);
	} else {
		sprintf(username, "%d", getuid());
	}
}

/* function to update the cached hostname */
void gethost() {
	struct utsname buf;

	uname(&buf);

	int len = strlen(buf.nodename);
	memcpy(_hostname, buf.nodename, len+1);
}

/* Draw the user prompt */
void draw_prompt(int ret) {
	/* Get the time */
	struct tm * timeinfo;
	struct timeval now;
	gettimeofday(&now, NULL); //time(NULL);
	timeinfo = localtime((time_t *)&now.tv_sec);

	/* Format the date and time for prompt display */
	char date_buffer[80];
	strftime(date_buffer, 80, "%m/%d", timeinfo);
	char time_buffer[80];
	strftime(time_buffer, 80, "%H:%M:%S", timeinfo);

	/* Print the working directory in there, too */
	getcwd(cwd, 512);
	char _cwd[512];
	strncpy(_cwd, cwd, 512);

	char * home = getenv("HOME");
	if (home && strstr(cwd, home) == cwd) {
		char * c = cwd + strlen(home);
		if (*c == '/' || *c == 0) {
			sprintf(_cwd, "~%s", c);
		}
	}

	/* Print the prompt. */
	printf("\033]1;%s@%s:%s\007", username, _hostname, _cwd);
	printf("\033[s\033[400C\033[16D\033[1m\033[38;5;59m[\033[38;5;173m%s \033[38;5;167m%s\033[38;5;59m]\033[u\033[38;5;221m%s\033[38;5;59m@\033[38;5;81m%s ",
			date_buffer, time_buffer,
			username, _hostname);
	if (ret != 0) {
		printf("\033[38;5;167m%d ", ret);
	}

	printf("\033[0m%s%s\033[0m ", _cwd, getuid() == 0 ? "\033[1;38;5;196m#" : "\033[1;38;5;47m$");
	fflush(stdout);
}

uint32_t child = 0;

void sig_pass(int sig) {
	/* Interrupt handler */
	if (child) {
		kill(child, sig);
	}
}

void redraw_prompt_func(rline_context_t * context) {
	draw_prompt(0);
}

void draw_prompt_c() {
	printf("> ");
	fflush(stdout);
}
void redraw_prompt_func_c(rline_context_t * context) {
	draw_prompt_c();
}

void tab_complete_func(rline_context_t * context) {
	char buf[1024];
	char * pch;
	char * cmd;
	char * save;

	memcpy(buf, context->buffer, 1024);

	pch = strtok_r(buf, " ", &save);
	cmd = pch;

	char * argv[1024];
	int argc = 0;

	if (!cmd) {
		argv[0] = "";
		argc = 1;
	} else {
		while (pch != NULL) {
			argv[argc] = (char *)pch;
			++argc;
			pch = strtok_r(NULL, " ", &save);
		}
	}

	argv[argc] = NULL;

	if (argc < 2) {
		if (context->buffer[strlen(context->buffer) - 1] == ' ' || argc == 0) {
			if (!context->tabbed) {
				context->tabbed = 1;
				return;
			}
			fprintf(stderr, "\n");
			for (int i = 0; i < shell_commands_len; ++i) {
				fprintf(stderr, "%s", shell_commands[i]);
				if (i < shell_commands_len - 1) {
					fprintf(stderr, ", ");
				}
			}
			fprintf(stderr, "\n");
			context->callbacks->redraw_prompt(context);
			rline_redraw(context);
			return;
		} else {
			int j = 0;
			list_t * matches = list_create();
			char * match = NULL;
			for (int i = 0; i < shell_commands_len; ++i) {
				if (strstr(shell_commands[i], argv[0]) == shell_commands[i]) {
					list_insert(matches, shell_commands[i]);
					match = shell_commands[i];
				}
			}
			if (matches->length == 0) {
				list_free(matches);
				return;
			} else if (matches->length == 1) {
				for (int j = 0; j < strlen(context->buffer); ++j) {
					printf("\010 \010");
				}
				printf("%s", match);
				fflush(stdout);
				memcpy(context->buffer, match, strlen(match) + 1);
				context->collected = strlen(context->buffer);
				context->offset = context->collected;
				list_free(matches);
				return;
			} else  {
				if (!context->tabbed) {
					context->tabbed = 1;
					list_free(matches);
					return;
				}
				j = matches->length;
				char tmp[1024];
				memcpy(tmp, argv[0], strlen(argv[0])+1);
				while (j == matches->length) {
					j = 0;
					int x = strlen(tmp);
					tmp[x] = match[x];
					tmp[x+1] = '\0';
					node_t * node;
					foreach(node, matches) {
						char * match = (char *)node->value;
						if (strstr(match, tmp) == match) {
							j++;
						}
					}
				}
				tmp[strlen(tmp)-1] = '\0';
				memcpy(context->buffer, tmp, strlen(tmp) + 1);
				context->collected = strlen(context->buffer);
				context->offset = context->collected;
				j = 0;
				fprintf(stderr, "\n");
				node_t * node;
				foreach(node, matches) {
					char * match = (char *)node->value;
					fprintf(stderr, "%s", match);
					++j;
					if (j < matches->length) {
						fprintf(stderr, ", ");
					}
				}
				fprintf(stderr, "\n");
				context->callbacks->redraw_prompt(context);
				fprintf(stderr, "\033[s");
				rline_redraw(context);
				list_free(matches);
				return;
			}
		}
	} else {
		/* XXX Should complete to file names here */
	}
}

void reverse_search(rline_context_t * context) {
	char input[512] = {0};
	int collected = 0;
	int start_at = 0;
	fprintf(stderr, "\033[G\033[s");
	fflush(stderr);
	key_event_state_t kbd_state = {0};
	while (1) {
		/* Find matches */
		char * match = "";
		int match_index = 0;
try_rev_search_again:
		if (collected) {
			for (int i = start_at; i < shell_history_count; i++) {
				char * c = shell_history_prev(i+1);
				if (strstr(c, input)) {
					match = c;
					match_index = i;
					break;
				}
			}
			if (!strcmp(match,"")) {
				if (start_at) {
					start_at = 0;
					goto try_rev_search_again;
				}
				collected--;
				input[collected] = '\0';
				if (collected) {
					goto try_rev_search_again;
				}
			}
		}
		fprintf(stderr, "\033[u(reverse-i-search)`%s': %s\033[K", input, match);
		fflush(stderr);

		uint32_t key_sym = kbd_key(&kbd_state, fgetc(stdin));
		switch (key_sym) {
			case KEY_BACKSPACE:
				if (collected > 0) {
					collected--;
					input[collected] = '\0';
					start_at = 0;
				}
				break;
			case KEY_CTRL_C:
				printf("^C\n");
				return;
			case KEY_CTRL_R:
				start_at = match_index + 1;
				break;
			case '\n':
				memcpy(context->buffer, match, strlen(match) + 1);
				context->collected = strlen(match);
				context->offset = context->collected;
				if (context->callbacks->redraw_prompt) {
					fprintf(stderr, "\033[G\033[K");
					context->callbacks->redraw_prompt(context);
				}
				fprintf(stderr, "\033[s");
				rline_redraw_clean(context);
				fprintf(stderr, "\n");
				return;
			default:
				if (key_sym < KEY_NORMAL_MAX) {
					input[collected] = (char)key_sym;
					collected++;
					input[collected] = '\0';
					start_at = 0;
				}
				break;
		}
	}
}

void history_previous(rline_context_t * context) {
	if (shell_scroll == 0) {
		memcpy(shell_temp, context->buffer, strlen(context->buffer) + 1);
	}
	if (shell_scroll < shell_history_count) {
		shell_scroll++;
		for (int i = 0; i < strlen(context->buffer); ++i) {
			printf("\010 \010");
		}
		char * h = shell_history_prev(shell_scroll);
		memcpy(context->buffer, h, strlen(h) + 1);
		printf("\033[u%s\033[K", h);
		fflush(stdout);
	}
	context->collected = strlen(context->buffer);
	context->offset = context->collected;
}

void history_next(rline_context_t * context) {
	if (shell_scroll > 1) {
		shell_scroll--;
		for (int i = 0; i < strlen(context->buffer); ++i) {
			printf("\010 \010");
		}
		char * h = shell_history_prev(shell_scroll);
		memcpy(context->buffer, h, strlen(h) + 1);
		printf("%s", h);
		fflush(stdout);
	} else if (shell_scroll == 1) {
		for (int i = 0; i < strlen(context->buffer); ++i) {
			printf("\010 \010");
		}
		shell_scroll = 0;
		memcpy(context->buffer, shell_temp, strlen(shell_temp) + 1);
		printf("\033[u%s\033[K", context->buffer);
		fflush(stdout);
	}
	context->collected = strlen(context->buffer);
	context->offset = context->collected;
}

void add_argument(list_t * argv, char * buf) {
	char * c = malloc(strlen(buf) + 1);
	memcpy(c, buf, strlen(buf) + 1);

	list_insert(argv, c);
}

int read_entry(char * buffer) {
	rline_callbacks_t callbacks = {
		tab_complete_func, redraw_prompt_func, NULL,
		history_previous, history_next,
		NULL, NULL, reverse_search
	};
	set_unbuffered();
	int buffer_size = rline((char *)buffer, LINE_LEN, &callbacks);
	set_buffered();
	return buffer_size;
}

int read_entry_continued(char * buffer) {
	rline_callbacks_t callbacks = {
		tab_complete_func, redraw_prompt_func_c, NULL,
		history_previous, history_next,
		NULL, NULL, reverse_search
	};
	set_unbuffered();
	int buffer_size = rline((char *)buffer, LINE_LEN, &callbacks);
	set_buffered();
	return buffer_size;
}

int variable_char(uint8_t c) {
	if (c >= 65 && c <= 90)  return 1;
	if (c >= 97 && c <= 122) return 1;
	if (c >= 48 && c <= 57)  return 1;
	if (c == 95) return 1;
	return 0;
}

void run_cmd(char ** args) {
	int i = execvp(*args, args);
	shell_command_t func = shell_find(*args);
	if (func) {
		int argc = 0;
		while (args[argc]) {
			argc++;
		}
		i = func(argc, args);
	} else {
		if (i != 0) {
			fprintf(stderr, "%s: Command not found\n", *args);
			i = 127;
		}
	}
	exit(i);
}

int shell_exec(char * buffer, int buffer_size) {

	/* Read previous history entries */
	if (buffer[0] == '!') {
		uint32_t x = atoi((char *)((uintptr_t)buffer + 1));
		if (x <= shell_history_count) {
			buffer = shell_history_get(x - 1);
			buffer_size = strlen(buffer);
		} else {
			fprintf(stderr, "esh: !%d: event not found\n", x);
			return 0;
		}
	}

	char * history = malloc(strlen(buffer) + 1);
	memcpy(history, buffer, strlen(buffer) + 1);

	if (buffer[0] != ' ' && buffer[0] != '\n') {
		shell_history_insert(history);
	} else {
		free(history);
	}

	char * argv[1024];
	int tokenid = 0;

	char quoted = 0;
	char backtick = 0;
	char buffer_[512] = {0};
	int collected = 0;

	list_t * args = list_create();

	while (1) {

		char * p = buffer;

		while (*p) {
			switch (*p) {
				case '$':
					if (quoted != '\'') {
						p++;
						char var[100];
						int  coll = 0;
						if (*p == '{') {
							p++;
							while (*p != '}' && *p != '\0' && (coll < 100)) {
								var[coll] = *p;
								coll++;
								var[coll] = '\0';
								p++;
							}
							if (*p == '}') {
								p++;
							}
						} else {
							while (*p != '\0' && variable_char(*p) && (coll < 100)) {
								var[coll] = *p;
								coll++;
								var[coll] = '\0';
								p++;
							}
						}
						char *c = getenv(var);
						if (c) {
							backtick = 0;
							for (int i = 0; i < strlen(c); ++i) {
								buffer_[collected] = c[i];
								collected++;
							}
							buffer_[collected] = '\0';
						}
						continue;
					}
					goto _next;
				case '\"':
					if (quoted == '\"') {
						if (backtick) {
							goto _just_add;
						}
						quoted = 0;
						goto _next;
					} else if (!quoted) {
						quoted = *p;
						goto _next;
					}
					goto _just_add;
				case '\'':
					if (quoted == '\'') {
						if (backtick) {
							goto _just_add;
						}
						quoted = 0;
						goto _next;
					} else if (!quoted) {
						quoted = *p;
						goto _next;
					}
					goto _just_add;
				case '\\':
					if (backtick) {
						goto _just_add;
					}
					backtick = 1;
					goto _next;
				case ' ':
					if (backtick) {
						goto _just_add;
					}
					if (!quoted) {
						goto _new_arg;
					}
					goto _just_add;
				case '\n':
					if (!quoted) {
						goto _done;
					}
					goto _just_add;
				case '|':
					if (!quoted && !backtick && !collected) {
						collected = sprintf(buffer_, "%s", PIPE_TOKEN);
						goto _new_arg;
					}
				default:
					if (backtick) {
						buffer_[collected] = '\\';
						collected++;
						buffer_[collected] = '\0';
					}
_just_add:
					backtick = 0;
					buffer_[collected] = *p;
					collected++;
					buffer_[collected] = '\0';
					goto _next;
			}

_new_arg:
			backtick = 0;
			if (collected) {
				add_argument(args, buffer_);
				buffer_[0] = '\0';
				collected = 0;
			}

_next:
			p++;
		}

_done:

		if (quoted) {
			if (shell_interactive) {
				draw_prompt_c();
				buffer_size = read_entry_continued(buffer);
				shell_history_append_line(buffer);
				continue;
			} else {
				fprintf(stderr, "Syntax error: Unterminated quoted string.\n");
				return 127;
			}
		}

		if (collected) {
			add_argument(args, buffer_);
			break;
		}

		break;
	}

	int cmdi = 0;
	char ** arg_starts[100] = { &argv[0], NULL };
	int argcs[100] = {0};

	int i = 0;
	foreach(node, args) {
		char * c = node->value;

		if (!strcmp(c, PIPE_TOKEN)) {
			argv[i] = 0;
			i++;
			cmdi++;
			arg_starts[cmdi] = &argv[i];
			continue;
		}

		argv[i] = c;
		i++;
		argcs[cmdi]++;
	}
	argv[i] = NULL;

	if (i == 0) {
		return 0;
	}

	list_free(args);

	char * cmd = *arg_starts[0];
	tokenid = i;

	unsigned int child_pid;

	int nowait = (!strcmp(argv[tokenid-1],"&"));
	if (nowait) {
		argv[tokenid-1] = NULL;
	}

	if (shell_force_raw) set_unbuffered();

	if (cmdi > 0) {
		int last_output[2];
		pipe(last_output);
		child_pid = fork();
		if (!child_pid) {
			dup2(last_output[1], STDOUT_FILENO);
			close(last_output[0]);
			run_cmd(arg_starts[0]);
		}

		for (int j = 1; j < cmdi; ++j) {
			int tmp_out[2];
			pipe(tmp_out);
			if (!fork()) {
				dup2(tmp_out[1], STDOUT_FILENO);
				dup2(last_output[0], STDIN_FILENO);
				close(tmp_out[0]);
				close(last_output[1]);
				run_cmd(arg_starts[j]);
			}
			close(last_output[0]);
			close(last_output[1]);
			last_output[0] = tmp_out[0];
			last_output[1] = tmp_out[1];
		}

		if (!fork()) {
			dup2(last_output[0], STDIN_FILENO);
			close(last_output[1]);
			run_cmd(arg_starts[cmdi]);
		}
		close(last_output[0]);
		close(last_output[1]);

		/* Now execute the last piece and wait on all of them */
	} else {
		shell_command_t func = shell_find(*arg_starts[0]);
		if (func) {
			return func(argcs[0], arg_starts[0]);
		} else {
			child_pid = fork();
			if (!child_pid) {
				run_cmd(arg_starts[0]);
			}
		}
	}

	tcsetpgrp(STDIN_FILENO, child_pid);
	int ret_code = 0;
	if (!nowait) {
		child = child_pid;
		int pid;
		do {
			pid = waitpid(-1, &ret_code, 0);
		} while (pid != -1 || (pid == -1 && errno != ECHILD));
		child = 0;
	}
	tcsetpgrp(STDIN_FILENO, getpid());
	free(cmd);
	return ret_code;
}

void add_path_contents() {
	DIR * dirp = opendir("/bin");

	struct dirent * ent = readdir(dirp);
	while (ent != NULL) {
		if (ent->d_name[0] != '.') {
			char * s = malloc(sizeof(char) * (strlen(ent->d_name) + 1));
			memcpy(s, ent->d_name, strlen(ent->d_name) + 1);
			shell_install_command(s, NULL);
		}

		ent = readdir(dirp);
	}
	closedir(dirp);

}

struct command {
	char * string;
	void * func;
};

static int comp_shell_commands(const void *p1, const void *p2) {
	return strcmp(((struct command *)p1)->string, ((struct command *)p2)->string);
}

void sort_commands() {
	struct command commands[SHELL_COMMANDS];
	for (int i = 0; i < shell_commands_len; ++i) {
		commands[i].string = shell_commands[i];
		commands[i].func   = shell_pointers[i];
	}
	qsort(&commands, shell_commands_len, sizeof(struct command), comp_shell_commands);
	for (int i = 0; i < shell_commands_len; ++i) {
		shell_commands[i] = commands[i].string;
		shell_pointers[i] = commands[i].func;
	}
}

void show_version(void) {
	printf("esh 0.11.0 - experimental shell\n");
}

void show_usage(int argc, char * argv[]) {
	printf(
			"Esh: The Experimental Shell\n"
			"\n"
			"usage: %s [-lha] [path]\n"
			"\n"
			" -c \033[4mcmd\033[0m \033[3mparse and execute cmd\033[0m\n"
			//-c cmd \033[...
			" -v     \033[3mshow version information\033[0m\n"
			" -?     \033[3mshow this help text\033[0m\n"
			"\n", argv[0]);
}


int main(int argc, char ** argv) {

	int  nowait = 0;
	int  free_cmd = 0;
	int  last_ret = 0;

	pid = getpid();

	signal(SIGINT, sig_pass);
	signal(SIGWINCH, sig_pass);

	getuser();
	gethost();

	install_commands();
	add_path_contents();
	sort_commands();

	if (argc > 1) {
		int index, c;
		while ((c = getopt(argc, argv, "c:v?")) != -1) {
			switch (c) {
				case 'c':
					shell_interactive = 0;
					return shell_exec(optarg, strlen(optarg));
				case 'v':
					show_version();
					return 0;
				case '?':
					show_usage(argc, argv);
					return 0;
			}
		}
	}

	shell_interactive = 1;

	while (1) {
		draw_prompt(last_ret);
		char buffer[LINE_LEN] = {0};
		int  buffer_size;

		buffer_size = read_entry(buffer);
		last_ret = shell_exec(buffer, buffer_size);
		shell_scroll = 0;

	}

exit:

	return 0;
}

/*
 * cd [path]
 */
uint32_t shell_cmd_cd(int argc, char * argv[]) {
	if (argc > 1) {
		if (chdir(argv[1])) {
			goto cd_error;
		} /* else success */
	} else /* argc < 2 */ {
		char * home = getenv("HOME");
		if (home) {
			if (chdir(home)) {
				goto cd_error;
			}
		} else {
			char home_path[512];
			sprintf(home_path, "/home/%s", username);
			if (chdir(home_path)) {
				goto cd_error;
			}
		}
	}
	return 0;
cd_error:
	fprintf(stderr, "%s: could not cd '%s': no such file or directory\n", argv[0], argv[1]);
	return 1;
}

/*
 * history
 */
uint32_t shell_cmd_history(int argc, char * argv[]) {
	for (int i = 0; i < shell_history_count; ++i) {
		printf("%d\t%s\n", i + 1, shell_history_get(i));
	}
	return 0;
}

uint32_t shell_cmd_test(int argc, char * argv[]) {
	printf("%d arguments.\n", argc);
	for (int i = 0; i < argc; ++i) {
		printf("%d -> %s\n", i, argv[i]);
	}
	return argc;
}

uint32_t shell_cmd_export(int argc, char * argv[]) {
	if (argc > 1) {
		putenv(argv[1]);
	}
	return 0;
}

uint32_t shell_cmd_exit(int argc, char * argv[]) {
	if (argc > 1) {
		exit(atoi(argv[1]));
	} else {
		exit(0);
	}
	return -1;
}

uint32_t shell_cmd_set(int argc, char * argv[]) {
	char * term = getenv("TERM");
	if (!term || strstr(term, "toaru") != term) {
		fprintf(stderr, "Unrecognized terminal. These commands are for the とある terminal only.\n");
		return 1;
	}
	if (argc < 2) {
		fprintf(stderr, "%s: expected argument\n", argv[0]);
		return 1;
	}

	if (!strcmp(argv[1], "alpha")) {
		if (argc < 3) {
			fprintf(stderr, "%s %s [0 or 1]\n", argv[0], argv[1]);
			return 1;
		}
		int i = atoi(argv[2]);
		if (i) {
			printf("\033[2001z");
		} else {
			printf("\033[2000z");
		}
		fflush(stdout);
		return 0;
	} else if (!strcmp(argv[1], "scale")) {
		if (argc < 3) {
			fprintf(stderr, "%s %s [floating point size, 1.0 = normal]\n", argv[0], argv[1]);
			return 1;
		}
		printf("\033[1555;%sz", argv[2]);
		fflush(stdout);
		return 0;
	} else if (!strcmp(argv[1], "size")) {
		if (argc < 4) {
			fprintf(stderr, "%s %s [width] [height]\n", argv[0], argv[1]);
			return 1;
		}
		printf("\033[3000;%s;%sz", argv[2], argv[3]);
		fflush(stdout);
		return 0;
	} else if (!strcmp(argv[1], "force-raw")) {
		shell_force_raw = 1;
		return 0;
	} else if (!strcmp(argv[1], "no-force-raw")) {
		shell_force_raw = 0;
		return 0;
	} else if (!strcmp(argv[1], "--help")) {
		fprintf(stderr, "Available arguments:\n"
		                "  alpha - alpha transparency enabled / disabled\n"
		                "  scale - font scaling\n"
		                "  size - terminal width/height in characters\n"
		                "  force-raw - sets terminal to raw mode before commands\n"
		                "  no-force-raw - disables forced raw mode\n"
		);
		return 0;
	}

	fprintf(stderr, "%s: unrecognized argument\n", argv[0]);
	return 1;
}

void install_commands() {
	shell_install_command("cd",      shell_cmd_cd);
	shell_install_command("history", shell_cmd_history);
	shell_install_command("export",  shell_cmd_export);
	shell_install_command("test",    shell_cmd_test);
	shell_install_command("exit",    shell_cmd_exit);
	shell_install_command("set",     shell_cmd_set);
}
