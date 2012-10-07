/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * E-Shell
 *
 * This is the "experimental shell". It provides
 * a somewhat unix-like shell environment, but does
 * not include a parser any advanced functionality.
 * It simply cuts its input into arguments and executes
 * programs.
 */

#include <stdio.h>
#include <stdint.h>
#include <syscall.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <dirent.h>

#include "lib/list.h"

#define SHELL_COMMANDS 512
typedef uint32_t(*shell_command_t) (int argc, char ** argv);
char * shell_commands[SHELL_COMMANDS];
shell_command_t shell_pointers[SHELL_COMMANDS];
uint32_t shell_commands_len = 0;

#define SHELL_HISTORY_ENTRIES 128
char * shell_history[SHELL_HISTORY_ENTRIES];
size_t shell_history_count  = 0;
size_t shell_history_offset = 0;

size_t shell_scroll = 0;
char   shell_temp[1024];

int pid;

char * shell_history_prev(size_t item);

void shell_history_insert(char * str) {
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

char * shell_history_get(size_t item) {
	return shell_history[(item + shell_history_offset) % SHELL_HISTORY_ENTRIES];
}

char * shell_history_prev(size_t item) {
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

void set_unbuffered() {
	printf("\033[1560z");
	fflush(stdout);
}

void set_buffered() {
	printf("\033[1561z");
	fflush(stdout);
}

void install_commands();

#define KBD_NORMAL 0
#define KBD_ESC_A  1
#define KBD_ESC_B  2
#define KBD_FUNC   3

int kbd_state = 0;

#define KEY_NONE        0
#define KEY_BACKSPACE   8
#define KEY_CTRL_C      3
#define KEY_CTRL_R      18
#define KEY_ESCAPE      27
#define KEY_NORMAL_MAX  256
#define KEY_ARROW_UP    257
#define KEY_ARROW_DOWN  258
#define KEY_ARROW_RIGHT 259
#define KEY_ARROW_LEFT  260
#define KEY_BAD_STATE   -1

uint32_t kbd_key(uint8_t c) {
	switch (kbd_state) {
		case KBD_NORMAL:
			switch (c) {
				case 0x1b:
					kbd_state = KBD_ESC_A;
					return KEY_NONE;
				default:
					return c;
			}
		case KBD_ESC_A:
			switch (c) {
				case 0x5b:
					kbd_state = KBD_ESC_B;
					return KEY_NONE;
				default:
					kbd_state = KBD_NORMAL;
					return c;
			}
		case KBD_ESC_B:
			switch (c) {
				case 0x41:
					kbd_state = KBD_NORMAL;
					return KEY_ARROW_UP;
				case 0x42:
					kbd_state = KBD_NORMAL;
					return KEY_ARROW_DOWN;
				case 0x43:
					kbd_state = KBD_NORMAL;
					return KEY_ARROW_RIGHT;
				case 0x44:
					kbd_state = KBD_NORMAL;
					return KEY_ARROW_LEFT;
				default:
				kbd_state = KBD_NORMAL;
				return c;
			}
		default:
			fprintf(stderr, "Keyboard in unknown state? Not sure what to do. kbd_state = %d\n", kbd_state);
			return KEY_BAD_STATE;
	}

	return KEY_BAD_STATE;
}


/* Maximum command length */
#define LINE_LEN 4096

/* Current working directory */
char cwd[1024] = {'/',0};

/* Timing type */
struct timeval {
	unsigned int tv_sec;
	unsigned int tv_usec;
};

/* Username */
char username[1024];

/* Hostname for prompt */
char _hostname[256];

/* function to update the cached username */
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

/* function to update the cached hostname */
void gethostname() {
	char buffer[256];
	size_t len = syscall_gethostname(buffer);
	memcpy(_hostname, buffer, len);
}

/* Draw the user prompt */
void draw_prompt(int ret) {
	/* Get the time */
	struct tm * timeinfo;
	struct timeval now;
	syscall_gettimeofday(&now, NULL); //time(NULL);
	timeinfo = localtime((time_t *)&now.tv_sec);

	/* Format the date and time for prompt display */
	char date_buffer[80];
	strftime(date_buffer, 80, "%m/%d", timeinfo);
	char time_buffer[80];
	strftime(time_buffer, 80, "%H:%M:%S", timeinfo);

	/* Print the prompt. */
	printf("\033[1m[\033[1;33m%s \033[1;32m%s \033[1;31m%s \033[1;34m%s\033[0m ",
			username, _hostname, date_buffer, time_buffer);
	if (ret != 0) {
		printf("\033[1;31m%d ", ret);
	}
	/* Print the working directory in there, too */
	syscall_getcwd(cwd, 1024);
	printf("\033[0m%s\033[1m]\033[0m\n\033[1;32m$\033[0m ", cwd);
	printf("\033]1;%s@%s:%s\007", username, _hostname, cwd);
	fflush(stdout);
}

uint32_t child = 0;

void sig_int(int sig) {
	/* Interrupt handler */
	if (child) {
		syscall_send_signal(child, sig);
	} else {
		fprintf(stderr, "XXX: Clear the buffer and re-render the prompt\n");
	}
}

typedef struct {
	char *  buffer;
	int     collected;
	int     requested;
	int     newline;
	int     cancel;
	int     offset;
	int     tabbed;
} rline_context_t;

typedef void (*rline_callback_t)(rline_context_t * context);

typedef struct {
	rline_callback_t tab_complete;
	rline_callback_t redraw_prompt;
	rline_callback_t special_key;
	rline_callback_t key_up;
	rline_callback_t key_down;
	rline_callback_t key_left;
	rline_callback_t key_right;
	rline_callback_t rev_search;
} rline_callbacks_t;

void rline_redraw(rline_context_t * context) {
	printf("%s", context->buffer);
	for (int i = context->offset; i < context->collected; ++i) {
		printf("\033[D");
	}
	fflush(stdout);
}

size_t rline(char * buffer, size_t buf_size, rline_callbacks_t * callbacks) {
	/* Initialize context */
	rline_context_t context = {
		buffer,
		0,
		buf_size,
		0,
		0,
		0,
		0,
	};

	/* Read keys */
	while ((context.collected < context.requested) && (!context.newline)) {
		uint32_t key_sym = kbd_key(fgetc(stdin));
		if (key_sym == KEY_NONE) continue;
		switch (key_sym) {
			case KEY_CTRL_C:
				printf("^C\n");
				context.buffer[0] = '\0';
				return 0;
			case KEY_CTRL_R:
				if (callbacks->rev_search) {
					callbacks->rev_search(&context);
					return context.collected;
				}
				continue;
			case KEY_ARROW_UP:
				if (callbacks->key_up) {
					callbacks->key_up(&context);
				}
				continue;
			case KEY_ARROW_DOWN:
				if (callbacks->key_down) {
					callbacks->key_down(&context);
				}
				continue;
			case KEY_ARROW_RIGHT:
				if (callbacks->key_right) {
					callbacks->key_right(&context);
				} else {
					if (context.offset < context.collected) {
						printf("\033[C");
						fflush(stdout);
						context.offset++;
					}
				}
				continue;
			case KEY_ARROW_LEFT:
				if (callbacks->key_left) {
					callbacks->key_left(&context);
				} else {
					if (context.offset > 0) {
						printf("\033[D");
						fflush(stdout);
						context.offset--;
					}
				}
				continue;
			case KEY_BACKSPACE:
				if (context.collected) {
					if (!context.offset) {
						continue;
					}
					printf("\010 \010");
					if (context.offset != context.collected) {
						int remaining = context.collected - context.offset;
						for (int i = 0; i < remaining; ++i) {
							printf("%c", context.buffer[context.offset + i]);
							context.buffer[context.offset + i - 1] = context.buffer[context.offset + i];
						}
						printf(" ");
						for (int i = 0; i < remaining + 1; ++i) {
							printf("\033[D");
						}
						context.offset--;
						context.collected--;
					} else {
						context.buffer[--context.collected] = '\0';
						context.offset--;
					}
					fflush(stdout);
				}
				continue;
			case 0x0C: /* ^L: Clear Screen, redraw prompt and buffer */
				printf("\033[H\033[2J");
				if (callbacks->redraw_prompt) {
					callbacks->redraw_prompt(&context);
				}
				rline_redraw(&context);
				continue;
			case '\t':
				if (callbacks->tab_complete) {
					callbacks->tab_complete(&context);
				}
				continue;
			case '\n':
				while (context.offset < context.collected) {
					printf("\033[C");
					context.offset++;
				}
				if (context.collected < context.requested) {
					context.buffer[context.collected] = '\n';
					context.buffer[++context.collected] = '\0';
					context.offset++;
				}
				printf("\n");
				fflush(stdout);
				context.newline = 1;
				continue;
		}
		if (context.offset != context.collected) {
			for (int i = context.collected; i > context.offset; --i) {
				context.buffer[i] = context.buffer[i-1];
			}
			if (context.collected < context.requested) {
				context.buffer[context.offset] = (char)key_sym;
				context.buffer[++context.collected] = '\0';
				context.offset++;
			}
			for (int i = context.offset - 1; i < context.collected; ++i) {
				printf("%c", context.buffer[i]);
			}
			for (int i = context.offset; i < context.collected; ++i) {
				printf("\033[D");
			}
			fflush(stdout);
		} else {
			printf("%c", (char)key_sym);
			if (context.collected < context.requested) {
				context.buffer[context.collected] = (char)key_sym;
				context.buffer[++context.collected] = '\0';
				context.offset++;
			}
			fflush(stdout);
		}
	}

	/* Cap that with a null */
	context.buffer[context.collected] = '\0';
	return context.collected;
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
			redraw_prompt_func(context);
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
				printf(match);
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
				redraw_prompt_func(context);
				rline_redraw(context);
				list_free(matches);
				return;
			}
		}
	} else {
		/* XXX Should complete to file names here */
		fprintf(stderr, "%d\n", argc);
	}
}

void reverse_search(rline_context_t * context) {
	char input[512] = {0};
	size_t collected = 0;
	int start_at = 0;
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
		fprintf(stderr, "\033[G(reverse-i-search)`%s': %s\033[K", input, match);
		fflush(stderr);
		uint32_t key_sym = kbd_key(fgetc(stdin));
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
				/* XXX execute */
				memcpy(context->buffer, match, strlen(match) + 1);
				context->collected = strlen(match);
				context->offset = context->collected;
				printf("\n");
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
		for (size_t i = 0; i < strlen(context->buffer); ++i) {
			printf("\010 \010");
		}
		char * h = shell_history_prev(shell_scroll);
		memcpy(context->buffer, h, strlen(h) + 1);
		printf("%s", h);
		fflush(stdout);
	}
	context->collected = strlen(context->buffer);
	context->offset = context->collected;
}

void history_next(rline_context_t * context) {
	if (shell_scroll > 1) {
		shell_scroll--;
		for (size_t i = 0; i < strlen(context->buffer); ++i) {
			printf("\010 \010");
		}
		char * h = shell_history_prev(shell_scroll);
		memcpy(context->buffer, h, strlen(h) + 1);
		printf("%s", h);
		fflush(stdout);
	} else if (shell_scroll == 1) {
		for (size_t i = 0; i < strlen(context->buffer); ++i) {
			printf("\010 \010");
		}
		shell_scroll = 0;
		memcpy(context->buffer, shell_temp, strlen(shell_temp) + 1);
		printf("%s", context->buffer);
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

size_t read_entry(char * buffer) {
	rline_callbacks_t callbacks = {
		tab_complete_func, redraw_prompt_func, NULL,
		history_previous, history_next,
		NULL, NULL, reverse_search
	};
	set_unbuffered();
	size_t buffer_size = rline((char *)buffer, LINE_LEN, &callbacks);
	set_buffered();
	return buffer_size;
}

size_t read_entry_continued(char * buffer) {
	rline_callbacks_t callbacks = {
		tab_complete_func, redraw_prompt_func_c, NULL,
		history_previous, history_next,
		NULL, NULL, reverse_search
	};
	set_unbuffered();
	size_t buffer_size = rline((char *)buffer, LINE_LEN, &callbacks);
	set_buffered();
	return buffer_size;
}

int shell_exec(char * buffer, size_t buffer_size) {

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
	int _argc = 0;
	char buffer_[512] = {0};
	int collected = 0;

	list_t * args = list_create();

	while (1) {

		char * p = buffer;

		while (*p) {
			switch (*p) {
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
				_argc++;
			}

_next:
			p++;
		}

_done:

		if (quoted) {
			draw_prompt_c();
			buffer_size = read_entry_continued(buffer);
			continue;
		}

		if (collected) {
			add_argument(args, buffer_);
			break;
		}

		break;
	}

	int i = 0;
	foreach(node, args) {
		char * c = node->value;

		argv[i] = c;
		i++;
	}

	if (i == 0) {
		return 0;
	}

	list_free(args);

	argv[i] = NULL;
	char * cmd = argv[0];
	tokenid = i;

	shell_command_t func = shell_find(argv[0]);

	if (func) {
		func(tokenid, argv);
	} else {
		FILE * file = NULL; //fopen(argv[0], "r");
		if (!strstr(argv[0],"/")) {
			cmd = malloc(sizeof(char) * (strlen(argv[0]) + strlen("/bin/") + 1));
			sprintf(cmd, "%s%s", "/bin/", argv[0]);
			file = fopen(cmd,"r");
			if (!file) {
				printf("Command not found: %s\n", argv[0]);
				free(cmd);
				return 1;
			}
			fclose(file);
		} else {
			file = fopen(argv[0], "r");
			if (!file) {
				printf("Command not found: %s\n", argv[0]);
				free(cmd);
				return 1;
			}
			fclose(file);
		}

		int nowait = (!strcmp(argv[tokenid-1],"&"));
		if (nowait) {
			argv[tokenid-1] = NULL;
		}


		uint32_t f = fork();
		if (getpid() != pid) {
			int i = execve(cmd, argv, NULL);
			return i;
		} else {
			int ret_code = 0;
			if (!nowait) {
				child = f;
				ret_code = syscall_wait(f);
				child = 0;
			}
			free(cmd);
			return ret_code;
		}
	}
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

int main(int argc, char ** argv) {
	int  nowait = 0;
	int  free_cmd = 0;
	int  last_ret = 0;

	pid = getpid();

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

	install_commands();
	add_path_contents();
	sort_commands();
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
			fprintf(stderr, "%s: could not cd '%s': no such file or directory\n", argv[0], argv[1]);
			return 1;
		} /* else success */
	} else /* argc < 2 */ {
		char home_path[512];
		sprintf(home_path, "/home/%s", username);
		if (chdir(home_path)) {
			fprintf(stderr, "%s: could not cd %s': no such file or directory\n", argv[0], argv[1]);
			return 1;
		}
	}
	return 0;
}

/*
 * history
 */
uint32_t shell_cmd_history(int argc, char * argv[]) {
	for (size_t i = 0; i < shell_history_count; ++i) {
		printf("%d\t%s\n", i + 1, shell_history_get(i));
	}
	return 0;
}

void install_commands() {
	shell_install_command("cd",      shell_cmd_cd);
	shell_install_command("history", shell_cmd_history);
}
