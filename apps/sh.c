/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2018 K. Lange
 *
 * E-Shell
 *
 * This is "experimental shell" - a vaguely-unix-like command
 * interface. It has a very rudimentary parser that understands
 * some things like pipes or writing out to a file. It has a
 * handful of built-in commands, including ones that implement
 * some more useful shell syntax such as loops and conditionals.
 * There is support for tab completion of filenames and commands.
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
#include <fcntl.h>
#include <ctype.h>

#include <sys/time.h>
#include <sys/wait.h>
#include <sys/utsname.h>
#include <sys/stat.h>

#include <toaru/list.h>
#include <toaru/kbd.h>
#include <toaru/rline.h>

#define PIPE_TOKEN "\xFF\xFFPIPE\xFF\xFF"
#define STAR_TOKEN "\xFF\xFFSTAR\xFF\xFF"
#define WRITE_TOKEN "\xFF\xFFWRITE\xFF\xFF"
#define APPEND_TOKEN "\xFF\xFF""APPEND\xFF"

/* A shell command is like a C program */
typedef uint32_t(*shell_command_t) (int argc, char ** argv);

/* We have a static array that fits a certain number of them. */
#define SHELL_COMMANDS 512
char * shell_commands[SHELL_COMMANDS];          /* Command names */
shell_command_t shell_pointers[SHELL_COMMANDS]; /* Command functions */
char * shell_descript[SHELL_COMMANDS];          /* Command descriptions */

/* This is the number of actual commands installed */
int shell_commands_len = 0;

int shell_interactive = 1;
int last_ret = 0;
char ** shell_argv = NULL;
int shell_argc = 0;


int pid; /* Process ID of the shell */

void shell_install_command(char * name, shell_command_t func, char * desc) {
	if (shell_commands_len == SHELL_COMMANDS) {
		fprintf(stderr, "Ran out of space for static shell commands. The maximum number of commands is %d\n", SHELL_COMMANDS);
		return;
	}
	shell_commands[shell_commands_len] = name;
	shell_pointers[shell_commands_len] = func;
	shell_descript[shell_commands_len] = desc;
	shell_commands_len++;
}

shell_command_t shell_find(char * str) {
	for (int i = 0; i < shell_commands_len; ++i) {
		if (!strcmp(str, shell_commands[i])) {
			return shell_pointers[i];
		}
	}
	return NULL;
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

void print_extended_ps(char * format) {
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

	/* Collect the current working directory */
	getcwd(cwd, 512);
	char _cwd[512];
	strcpy(_cwd, cwd);

	/* Collect the user's home directory and apply it to cwd */
	char * home = getenv("HOME");
	if (home && strstr(cwd, home) == cwd) {
		char * c = cwd + strlen(home);
		if (*c == '/' || *c == 0) {
			sprintf(_cwd, "~%s", c);
		}
	}

	char ret[80] = {0};
	if (last_ret != 0) {
		sprintf(ret, "%d ", last_ret);
	}

	while (*format) {
		if (*format == '\\') {
			format++;
			switch (*format) {
				case '\\':
					putchar(*format);
					format++;
					break;
				case '0':
				case '1':
				case '2':
				case '3':
				case '4':
				case '5':
				case '6':
				case '7':
					{
						int i = (*format) - '0';
						format++;
						if (*format >= '0' && *format <= '7') {
							i *= 8;
							i += (*format) - '0';
							format++;
							if (*format >= '0' && *format <= '7') {
								i *= 8;
								i += (*format) - '0';
								format++;
							}
						}
						putchar(i);
					}
					break;
				case 'e':
					putchar('\033');
					format++;
					break;
				case 'd':
					printf("%s", date_buffer);
					format++;
					break;
				case 't':
					printf("%s", time_buffer);
					format++;
					break;
				case 'h':
					printf("%s", _hostname);
					format++;
					break;
				case 'u':
					printf("%s", username);
					format++;
					break;
				case 'w':
					printf("%s", _cwd);
					format++;
					break;
				case '$':
					putchar(getuid() == 0 ? '#' : '$');
					format++;
					break;
				case 'U': /* prompt color string */
					printf("%s", getuid() == 0 ? "\033[1;38;5;196m" : "\033[1;38;5;47m");
					format++;
					break;
				case 'r':
					printf("%s", ret);
					format++;
					break;
				default:
					printf("\\%c", *format);
					format++;
					break;
			}
		} else {
			putchar(*format);
			format++;
		}
	}

}

#define FALLBACK_PS1 "\\u@\\h \\w\\$ "

/* Draw the user prompt */
void draw_prompt(void) {
	char * ps1 = getenv("PS1");
	print_extended_ps(ps1 ? ps1 : FALLBACK_PS1);
	fflush(stdout);
}

volatile int break_while = 0;
uint32_t child = 0;

void sig_pass(int sig) {
	/* Interrupt handler */
	if (child) {
		kill(child, sig);
	}
	break_while = sig;
}

void redraw_prompt_func(rline_context_t * context) {
	draw_prompt();
}

void draw_prompt_c() {
	char * ps2 = getenv("PS2");
	if (ps2) {
		print_extended_ps(ps2);
	} else {
		printf("> ");
	}
	fflush(stdout);
}
void redraw_prompt_func_c(rline_context_t * context) {
	draw_prompt_c();
}

void tab_complete_func(rline_context_t * c) {
	char * dup = malloc(LINE_LEN);
	
	memcpy(dup, c->buffer, LINE_LEN);

	char *pch, *save;
	char *argv[1024];
	int argc = 0;
	int cursor = 0;

	pch = strtok_r(dup, " ", &save);

	if (!pch) {
		argv[0] = "";
		argc = 0;
	}

	while (pch != NULL) {
		if (pch - dup <= c->offset) cursor = argc;
		argv[argc] = pch;
		++argc;
		pch = strtok_r(NULL, " ", &save);
	}
	argv[argc] = NULL;

	if (c->offset && c->buffer[c->offset-1] == ' ' && argc) {
		cursor++;
	}

	char * word = argv[cursor];
	int word_offset = word ? (c->offset - (argv[cursor] - dup)) : 0;

	char * prefix = malloc(word_offset + 1);
	if (word) memcpy(prefix, word, word_offset);
	prefix[word_offset] = '\0';

	/* Complete file path */
	list_t * matches = list_create();
	char * match = NULL;
	int free_matches = 0;
	int no_space_if_only = 0;

	/* TODO custom auto-complete as a configuration file? */
#define COMPLETE_FILE    1
#define COMPLETE_COMMAND 2
#define COMPLETE_CUSTOM  3
#define COMPLETE_VARIABLE 4
	int complete_mode = COMPLETE_FILE;

	int command_adj = 0;
	int cursor_adj = cursor;

	/* sudo should shift commands */
	if (cursor_adj > command_adj && (!strcmp(argv[command_adj], "sudo") || !strcmp(argv[command_adj], "gsudo"))) {
		cursor_adj -= 1;
		command_adj += 1;
	}

	/* initial tab completion should be commands, unless typing a file path */
	if (cursor_adj == 0 && !strchr(prefix,'/')) {
		complete_mode = COMPLETE_COMMAND;
	}

	if (cursor_adj >= 1 && !strcmp(argv[command_adj], "toggle-abs-mouse")) {
		complete_mode = COMPLETE_CUSTOM;
	}


	/* complete variable names */
	if (*prefix == '$') {
		complete_mode = COMPLETE_VARIABLE;
	}

	if (complete_mode == COMPLETE_COMMAND) {
		/* Complete a command name */

		for (int i = 0; i < shell_commands_len; ++i) {
			if (strstr(shell_commands[i], prefix) == shell_commands[i]) {
				list_insert(matches, shell_commands[i]);
				match = shell_commands[i];
			}
		}
	} else if (complete_mode == COMPLETE_FILE) {
		/* Complete a file path */

		free_matches = 1;
		char * tmp = strdup(prefix);
		char * last_slash = strrchr(tmp, '/');
		DIR * dirp;
		char * compare = prefix;
		if (last_slash) {
			*last_slash = '\0';
			word = word + (last_slash - tmp) + 1;
			word_offset = word_offset - (last_slash - tmp + 1);
			compare = word;
			if (last_slash == tmp) {
				dirp = opendir("/");
			} else {
				dirp = opendir(tmp);
			}
		} else {
			dirp = opendir(".");
		}

		if (!dirp) {
			free(tmp);
			goto finish_tab;
		}

		struct dirent * ent = readdir(dirp);
		while (ent != NULL) {
			if (ent->d_name[0] != '.') {
				if (!word || strstr(ent->d_name, compare) == ent->d_name) {
					struct stat statbuf;
					/* stat it */
					if (last_slash) {
						char * x = malloc(strlen(tmp) + 1 + strlen(ent->d_name) + 1);
						sprintf(x,"%s/%s",tmp,ent->d_name);
						lstat(x, &statbuf);
						free(x);
					} else {
						lstat(ent->d_name, &statbuf);
					}
					char * s;
					if (S_ISDIR(statbuf.st_mode)) {
						s = malloc(strlen(ent->d_name) + 2);
						sprintf(s,"%s/", ent->d_name);
						no_space_if_only = 1;
					} else {
						s = strdup(ent->d_name);
					}
					list_insert(matches, s);
					match = s;
				}
			}
			ent = readdir(dirp);
		}
		closedir(dirp);

		free(tmp);
	} else if (complete_mode == COMPLETE_CUSTOM) {

		char ** completions = NULL;
		char * toggle_abs_mouse_completions[] = {"relative","absolute",NULL};

		if (!strcmp(argv[command_adj],"toggle-abs-mouse")) {
			completions = toggle_abs_mouse_completions;
		}

		while (*completions) {
			if (strstr(*completions, prefix) == *completions) {
				list_insert(matches, *completions);
				match = *completions;
			}
			completions++;
		}

	} else if (complete_mode == COMPLETE_VARIABLE) {

		char ** envvar = environ;
		free_matches = 1;

		while (*envvar) {
			char * tmp = strdup(*envvar);
			char * c = strchr(tmp, '=');
			*c = '\0';
			if (strstr(tmp, prefix+1) == tmp) {
				char * m = malloc(strlen(tmp)+2);
				sprintf(m, "$%s", tmp);
				list_insert(matches, m);
				match = m;
			}
			free(tmp);
			envvar++;
		}

	}

	if (matches->length == 1) {
		/* Insert */
		rline_insert(c, &match[word_offset]);
		if (word && word_offset == (int)strlen(word) && !no_space_if_only) {
			rline_insert(c, " ");
		}
		rline_redraw(c);
	} else if (matches->length > 1) {
		if (!c->tabbed) {
			/* see if there is a minimum subset we can fill in */
			size_t j = word_offset;
			do {
				char d = match[j];
				int diff = 0;
				foreach(node, matches) {
					char * match = (char *)node->value;
					if (match[j] != d || match[j] == '\0') diff = 1;
				}
				if (diff) break;
				j++;
			} while (j < (size_t)c->requested);
			if (j > (size_t)word_offset) {
				char * tmp = strdup(match);
				tmp[j] = '\0';
				rline_insert(c, &tmp[word_offset]);
				rline_redraw(c);
				free(tmp);
			} else {
				c->tabbed = 1;
			}
		} else {
			/* Print matches */
			fprintf(stderr,"\n");
			size_t j = 0;
			foreach(node, matches) {
				char * match = (char *)node->value;
				fprintf(stderr, "%s", match);
				++j;
				if (j < matches->length) {
					fprintf(stderr, ", ");
				}
			}
			fprintf(stderr,"\n");
			c->callbacks->redraw_prompt(c);
			fprintf(stderr, "\033[s");
			rline_redraw(c);
		}
	}

finish_tab:
	if (free_matches) list_destroy(matches);
	list_free(matches);
	free(prefix);
	free(dup);

}

void add_argument(list_t * argv, char * buf) {
	char * c = malloc(strlen(buf) + 1);
	memcpy(c, buf, strlen(buf) + 1);

	list_insert(argv, c);
}

int read_entry(char * buffer) {
	rline_callbacks_t callbacks = {
		tab_complete_func, redraw_prompt_func, NULL,
		NULL, NULL, NULL, NULL, NULL
	};
	int buffer_size = rline((char *)buffer, LINE_LEN, &callbacks);
	return buffer_size;
}

int read_entry_continued(char * buffer) {
	rline_callbacks_t callbacks = {
		tab_complete_func, redraw_prompt_func_c, NULL,
		NULL, NULL, NULL, NULL, NULL
	};
	int buffer_size = rline((char *)buffer, LINE_LEN, &callbacks);
	return buffer_size;
}

int variable_char(uint8_t c) {
	if (c >= 'A' && c <= 'Z')  return 1;
	if (c >= 'a' && c <= 'z') return 1;
	if (c >= '0' && c <= '9')  return 1;
	if (c == '_') return 1;
	if (c == '?') return 1;
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

int is_number(const char * c) {
	while (*c) {
		if (!isdigit(*c)) return 0;
		c++;
	}
	return 1;
}

int shell_exec(char * buffer, size_t size, FILE * file, char ** out_buffer) {

	*out_buffer = NULL;

	/* Read previous history entries */
	if (buffer[0] == '!') {
		int x = atoi((char *)((uintptr_t)buffer + 1));
		if (x > 0 && x <= rline_history_count) {
			buffer = rline_history_get(x - 1);
		} else {
			fprintf(stderr, "esh: !%d: event not found\n", x);
			return 0;
		}
	}

	char * argv[1024];
	int tokenid = 0;

	char quoted = 0;
	char backtick = 0;
	char buffer_[512] = {0};
	int collected = 0;

	list_t * args = list_create();
	int have_star = 0;

	while (1) {

		char * p = buffer;

		while (*p) {
			switch (*p) {
				case '$':
					if (quoted == '\'') {
						goto _just_add;
					} else {
						if (backtick) {
							goto _just_add;
						}
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
								if (coll == 0 && (isdigit(*p) || *p == '?')) {
									p++;
									break; /* Don't let these keep going */
								}
								p++;
							}
						}
						/* Special cases */
						char *c = NULL;
						char tmp[128];
						if (!strcmp(var, "?")) {
							sprintf(tmp,"%d",last_ret);
							c = tmp;
						} else if (is_number(var)) {
							int a = atoi(var);
							if (a >= 0 && a < shell_argc) {
								c = shell_argv[a];
							}
						} else {
							c = getenv(var);
						}

						if (c) {
							backtick = 0;
							for (int i = 0; i < (int)strlen(c); ++i) {
								if (c[i] == ' ' && !quoted) {
									/* If we are not quoted and we reach a space, it signals a new argument */
									if (collected) {
										buffer_[collected] = '\0';
										add_argument(args, buffer_);
										buffer_[0] = '\0';
										have_star = 0;
										collected = 0;
									}
								} else {
									buffer_[collected] = c[i];
									collected++;
								}
							}
							buffer_[collected] = '\0';
						}
						continue;
					}
				case '\"':
					if (quoted == '\"') {
						if (backtick) {
							goto _just_add;
						}
						quoted = 0;
						goto _next;
					} else if (!quoted) {
						if (backtick) {
							goto _just_add;
						}
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
						if (backtick) {
							goto _just_add;
						}
						quoted = *p;
						goto _next;
					}
					goto _just_add;
				case '*':
					if (quoted) {
						goto _just_add;
					}
					if (backtick) {
						goto _just_add;
					}
					if (have_star) {
						goto _just_add; /* TODO multiple globs */
					}
					have_star = 1;
					collected += sprintf(&buffer_[collected], STAR_TOKEN);
					goto _next;
				case '\\':
					if (quoted == '\'') {
						goto _just_add;
					}
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
					if (!quoted && !backtick) {
						if (collected) {
							add_argument(args, buffer_);
						}
						collected = sprintf(buffer_, "%s", PIPE_TOKEN);
						goto _new_arg;
					}
					goto _just_add;
				case '>':
					if (!quoted && !backtick) {
						if (collected) {
							add_argument(args, buffer_);
						}
						collected = sprintf(buffer_, "%s", WRITE_TOKEN);
						goto _new_arg;
					}
					goto _just_add;
				case ';':
					if (!quoted && !backtick) {
						*out_buffer = ++p;
						goto _done;
					}
				case '#':
					if (!quoted && !backtick) {
						goto _done; /* Support comments; must not be part of an existing arg */
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
				have_star = 0;
				collected = 0;
			}

_next:
			p++;
		}

_done:

		if (quoted || backtick) {
			backtick = 0;
			if (shell_interactive == 1) {
				draw_prompt_c();
				read_entry_continued(buffer);
				rline_history_append_line(buffer);
				continue;
			} else if (shell_interactive == 2) {
				fgets(buffer, size, file);
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
	char * output_files[100] = { NULL };
	int file_args[100] = {0};
	int argcs[100] = {0};
	int next_is_file = 0;

	int i = 0;
	foreach(node, args) {
		char * c = node->value;

		if (next_is_file) {
			if (next_is_file == 1 && !strcmp(c, WRITE_TOKEN)) {
				next_is_file = 2;
				file_args[cmdi] = O_WRONLY | O_CREAT | O_APPEND;
				continue;
			}
			output_files[cmdi] = c;
			continue;
		}

		if (!strcmp(c, WRITE_TOKEN)) {
			next_is_file = 1;
			file_args[cmdi] = O_WRONLY | O_CREAT | O_TRUNC;
			continue;
		}

		if (!strcmp(c, PIPE_TOKEN)) {
			if (arg_starts[cmdi] == &argv[i]) {
				fprintf(stderr, "Syntax error: Unexpected pipe token\n");
				return 2;
			}
			argv[i] = 0;
			i++;
			cmdi++;
			arg_starts[cmdi] = &argv[i];
			continue;
		}

		char * glob = strstr(c, STAR_TOKEN);
		if (glob) {
			/* Globbing */
			glob[0] = '\0';
			glob[1] = '\0';

			char * before = c;
			char * after = &glob[8];

			int has_before = !!strlen(before);
			int has_after = !!strlen(after);

			if (!has_before || !strchr(before,'/')) {
				/* read current directory, add all */
				DIR * dirp = opendir(".");

				int before_i = i;
				struct dirent * ent = readdir(dirp);
				while (ent != NULL) {
					if (ent->d_name[0] != '.') {
						char * s = malloc(sizeof(char) * (strlen(ent->d_name) + 1));
						memcpy(s, ent->d_name, strlen(ent->d_name) + 1);

						char * t = s;

						if (has_before) {
							if (strstr(s,before) != s) {
								goto _nope;
							}
							t = &s[strlen(before)];
						}
						if (has_after) {
							if (strlen(t) >= strlen(after)) {
								if (!strcmp(after,&t[strlen(t)-strlen(after)])) {
									argv[i] = s;
									i++;
									argcs[cmdi]++;
								}
							}
						} else {
							argv[i] = s;
							i++;
							argcs[cmdi]++;
						}
					}
_nope:
					ent = readdir(dirp);
				}
				closedir(dirp);

				if (before_i == i) {
					/* no matches */
					glob[0] = '*';
					memmove(&glob[1], after, strlen(after)+1);
					argv[i] = c;
					i++;
					argcs[cmdi]++;
				} else {
					free(c);
				}
			} else {
				/* directory globs not supported */
				glob[0] = '*';
				argv[i] = c;
				i++;
				argcs[cmdi]++;
			}
		} else {
			argv[i] = c;
			i++;
			argcs[cmdi]++;
		}
	}
	argv[i] = NULL;

	if (i == 0) {
		return -1;
	}

	list_free(args);

	if (!*arg_starts[cmdi]) {
		fprintf(stderr, "Syntax error: Unexpected end of input\n");
		return 2;
	}

	char * cmd = *arg_starts[0];
	tokenid = i;

	unsigned int child_pid;

	int nowait = (!strcmp(argv[tokenid-1],"&"));
	if (nowait) {
		argv[tokenid-1] = NULL;
	}

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
			if (output_files[cmdi]) {
				dup2(open(output_files[cmdi], file_args[cmdi], 0666), STDOUT_FILENO);
			}
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
				if (output_files[cmdi]) {
					dup2(open(output_files[cmdi], file_args[cmdi], 0666), STDOUT_FILENO);
				}
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

void add_path_contents(char * path) {
	DIR * dirp = opendir(path);

	if (!dirp) return; /* Failed to load directly */

	struct dirent * ent = readdir(dirp);
	while (ent != NULL) {
		if (ent->d_name[0] != '.') {
			char * s = malloc(sizeof(char) * (strlen(ent->d_name) + 1));
			memcpy(s, ent->d_name, strlen(ent->d_name) + 1);
			shell_install_command(s, NULL, NULL);
		}

		ent = readdir(dirp);
	}
	closedir(dirp);

}

struct command {
	char * string;
	void * func;
	char * desc;
};

static int comp_shell_commands(const void *p1, const void *p2) {
	return strcmp(((struct command *)p1)->string, ((struct command *)p2)->string);
}

void sort_commands() {
	struct command commands[SHELL_COMMANDS];
	for (int i = 0; i < shell_commands_len; ++i) {
		commands[i].string = shell_commands[i];
		commands[i].func   = shell_pointers[i];
		commands[i].desc   = shell_descript[i];
	}
	qsort(&commands, shell_commands_len, sizeof(struct command), comp_shell_commands);
	for (int i = 0; i < shell_commands_len; ++i) {
		shell_commands[i] = commands[i].string;
		shell_pointers[i] = commands[i].func;
		shell_descript[i] = commands[i].desc;
	}
}

void show_version(void) {
	printf("esh 1.3.0\n");
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

void add_path(void) {

	char * envvar = getenv("PATH");

	if (!envvar) {
		add_path_contents("/bin");
		return;
	}

	char * tmp = strdup(envvar);

	do {
		char * end = strstr(tmp,":");
		if (end) {
			*end = '\0';
			end++;
		}
		add_path_contents(tmp);
		tmp = end;
	} while (tmp);

	free(tmp);
}

int run_script(FILE * f) {
	while (!feof(f)) {
		char buf[LINE_LEN] = {0};
		fgets(buf, LINE_LEN, f);
		int ret;
		char * out = NULL;
		char * b = buf;
		do {
			ret = shell_exec(b, LINE_LEN, f, &out);
			b = out;
		} while (b);
		if (ret >= 0) last_ret = ret;
	}

	fclose(f);

	return last_ret;
}

void source_eshrc(void) {
	char * home = getenv("HOME");

	if (!home) return;

	char tmp[512];
	sprintf(tmp, "%s/.eshrc", home);

	FILE * f = fopen(tmp, "r");
	if (!f) return;

	run_script(f);
}

int main(int argc, char ** argv) {

	pid = getpid();

	signal(SIGINT, sig_pass);
	signal(SIGWINCH, sig_pass);

	getuser();
	gethost();

	install_commands();
	/* Parse $PATH to add contents */
	add_path();
	sort_commands();

	if (argc > 1) {
		int c;
		while ((c = getopt(argc, argv, "c:v?")) != -1) {
			switch (c) {
				case 'c':
					shell_interactive = 0;
					{
						char * out = NULL;
						do {
							last_ret = shell_exec(optarg, strlen(optarg), NULL, &out);
							optarg = out;
						} while (optarg);
					}
					return (last_ret == -1) ? 0 : last_ret;
				case 'v':
					show_version();
					return 0;
				case '?':
					show_usage(argc, argv);
					return 0;
			}
		}
	}

	if (optind < argc) {
		shell_interactive = 2;
		FILE * f = fopen(argv[optind],"r");

		if (!f) {
			fprintf(stderr, "%s: %s: %s\n", argv[0], argv[optind], strerror(errno));
			return 1;
		}

		shell_argc = argc - 1;
		shell_argv = &argv[1];

		return run_script(f);
	}

	shell_interactive = 1;

	source_eshrc();

	while (1) {
		draw_prompt();
		char buffer[LINE_LEN] = {0};

		read_entry(buffer);

		char * history = malloc(strlen(buffer) + 1);
		memcpy(history, buffer, strlen(buffer) + 1);

		if (buffer[0] != ' ' && buffer[0] != '\n' && buffer[0] != '!') {
			rline_history_insert(history);
		} else {
			free(history);
		}

		int ret;
		char * out = NULL;
		char * b = buffer;
		do {
			ret = shell_exec(b, LINE_LEN, stdin, &out);
			b = out;
		} while (b);
		if (ret >= 0) last_ret = ret;
		rline_scroll = 0;

	}

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
	for (int i = 0; i < rline_history_count; ++i) {
		printf("%d\t%s\n", i + 1, rline_history_get(i));
	}
	return 0;
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

uint32_t shell_cmd_help(int argc, char * argv[]) {
	show_version();
	printf("\nThis shell is not POSIX-compliant, please be careful.\n\n");
	printf("Built-in commands:\n");

	/* First, determine max width of command names */
	unsigned int max_len = 0;
	for (int i = 0; i < shell_commands_len; ++i) {
		if (!shell_descript[i]) continue;
		if (strlen(shell_commands[i]) > max_len) {
			max_len = strlen(shell_commands[i]);
		}
	}

	/* Then print the commands their help text */
	for (int i = 0; i < shell_commands_len; ++i) {
		if (!shell_descript[i]) continue;
		printf(" %-*s - %s\n", max_len + 1, shell_commands[i], shell_descript[i]);
	}

	return 0;
}

uint32_t shell_cmd_if(int argc, char * argv[]) {
	char ** if_args = &argv[1];
	char ** then_args = NULL;
	char ** else_args = NULL;

	for (int i = 2; i < argc; ++i) {
		if (!strcmp(argv[i],"then")) {
			argv[i] = NULL;
			then_args = &argv[i+1];
		} else if (!strcmp(argv[i],"else")) {
			argv[i] = NULL;
			else_args = &argv[i+1];
		}
	}

	if (!then_args) {
		fprintf(stderr, "%s: syntax error: expected 'then' clause\n", argv[0]);
		return 1;
	}

	if (else_args && else_args < then_args) {
		fprintf(stderr, "%s: syntax error: 'else' clause before 'then' clase\n", argv[0]);
		return 1;
	}

	pid_t child_pid = fork();
	if (!child_pid) {
		run_cmd(if_args);
	}
	tcsetpgrp(STDIN_FILENO, child_pid);

	child = child_pid;

	int pid, ret_code = 0;
	do {
		pid = waitpid(-1, &ret_code, 0);
	} while (pid != -1 || (pid == -1 && errno != ECHILD));

	child = 0;

	if (ret_code == 0) {
		shell_command_t func = shell_find(*then_args);
		if (func) {
			int argc = 0;
			while (then_args[argc]) {
				argc++;
			}
			return func(argc, then_args);
		} else {
			child_pid = fork();
			if (!child_pid) {
				run_cmd(then_args);
			}
			tcsetpgrp(STDIN_FILENO, child_pid);
			child = child_pid;
			do {
				pid = waitpid(-1, &ret_code, 0);
			} while (pid != -1 || (pid == -1 && errno != ECHILD));
			child = 0;
			tcsetpgrp(STDIN_FILENO, getpid());
			return ret_code;
		}
	} else if (else_args) {
		shell_command_t func = shell_find(*else_args);
		if (func) {
			int argc = 0;
			while (else_args[argc]) {
				argc++;
			}
			return func(argc, else_args);
		} else {
			child_pid = fork();
			if (!child_pid) {
				run_cmd(else_args);
			}
			tcsetpgrp(STDIN_FILENO, child_pid);
			child = child_pid;
			do {
				pid = waitpid(-1, &ret_code, 0);
			} while (pid != -1 || (pid == -1 && errno != ECHILD));
			child = 0;
			return ret_code;
		}
	}

	tcsetpgrp(STDIN_FILENO, getpid());
	return 0;
}

uint32_t shell_cmd_while(int argc, char * argv[]) {
	char ** while_args = &argv[1];
	char ** do_args = NULL;

	for (int i = 2; i < argc; ++i) {
		if (!strcmp(argv[i],"do")) {
			argv[i] = NULL;
			do_args = &argv[i+1];
		}
	}

	if (!do_args) {
		fprintf(stderr, "%s: syntax error: expected 'do' clause\n", argv[0]);
		return 1;
	}

	break_while = 0;
	tcsetpgrp(STDIN_FILENO, getpid());

	do {
		pid_t child_pid = fork();
		if (!child_pid) {
			run_cmd(while_args);
		}
		child = child_pid;

		int pid, ret_code = 0;
		do {
			pid = waitpid(-1, &ret_code, 0);
		} while (pid != -1 || (pid == -1 && errno != ECHILD));
		child = 0;

		if (ret_code == 0) {
			child_pid = fork();
			if (!child_pid) {
				run_cmd(do_args);
			}
			child = child_pid;
			do {
				pid = waitpid(-1, &ret_code, 0);
			} while (pid != -1 || (pid == -1 && errno != ECHILD));
			child = 0;
		} else {
			return ret_code;
		}
	} while (!break_while);

	return 127;
}

uint32_t shell_cmd_export_cmd(int argc, char * argv[]) {

	if (argc < 3) {
		fprintf(stderr, "%s: syntax error: not enough arguments\n", argv[0]);
		return 1;
	}

	int pipe_fds[2];
	pipe(pipe_fds);
	pid_t child_pid = fork();
	if (!child_pid) {
		dup2(pipe_fds[1], STDOUT_FILENO);
		close(pipe_fds[0]);
		run_cmd(&argv[2]);
	}

	close(pipe_fds[1]);

	tcsetpgrp(STDIN_FILENO, child_pid);
	char buf[1024];
	size_t accum = 0;

	do {
		int r = read(pipe_fds[0], buf+accum, 1023-accum);

		if (r == 0) break;
		if (r < 0) {
			return -r;
		}

		accum += r;
	} while (accum < 1023);

	tcsetpgrp(STDIN_FILENO, getpid());

	buf[accum] = '\0';

	if (accum && buf[accum-1] == '\n') {
		buf[accum-1] = '\0';
	}

	setenv(argv[1], buf, 1);
	return 0;
}

uint32_t shell_cmd_empty(int argc, char * argv[]) {

	for (int i = 1; i < argc; i++) {
		if (argv[i] && *argv[i]) return 1;
	}

	return 0;
}

uint32_t shell_cmd_equals(int argc, char * argv[]) {

	if (argc < 3) return 1;

	return strcmp(argv[1], argv[2]);
}

uint32_t shell_cmd_return(int argc, char * argv[]) {
	if (argc < 2) return 0;

	return atoi(argv[1]);
}

uint32_t shell_cmd_source(int argc, char * argv[]) {
	if (argc < 2) return 0;

	FILE * f = fopen(argv[1], "r");

	if (!f) {
		fprintf(stderr, "%s: %s: %s", argv[0], argv[1], strerror(errno));
	}

	return run_script(f);
}

uint32_t shell_cmd_exec(int argc, char * argv[]) {
	if (argc < 2) return 1;
	return execvp(argv[1], &argv[1]);
}

uint32_t shell_cmd_not(int argc, char * argv[]) {
	if (argc < 2) {
		fprintf(stderr, "%s: expected command argument\n", argv[0]);
		return 1;
	}
	int ret_code = 0;
	pid_t child_pid = fork();
	if (!child_pid) {
		run_cmd(&argv[1]);
	}
	tcsetpgrp(STDIN_FILENO, child_pid);
	child = child_pid;
	do {
		pid = waitpid(-1, &ret_code, 0);
	} while (pid != -1 || (pid == -1 && errno != ECHILD));
	child = 0;
	tcsetpgrp(STDIN_FILENO, getpid());
	return !ret_code;

}

void install_commands() {
	shell_install_command("cd",      shell_cmd_cd, "change directory");
	shell_install_command("exit",    shell_cmd_exit, "exit the shell");
	shell_install_command("export",  shell_cmd_export, "set environment variables: export VAR=value");
	shell_install_command("help",    shell_cmd_help, "display this help text");
	shell_install_command("history", shell_cmd_history, "list command history");
	shell_install_command("if",      shell_cmd_if, "if ... then ... [else ...]");
	shell_install_command("while",   shell_cmd_while, "while ... do ...");
	shell_install_command("empty?",  shell_cmd_empty, "empty? args...");
	shell_install_command("equals?", shell_cmd_equals, "equals? arg1 arg2");
	shell_install_command("return",  shell_cmd_return, "return status code");
	shell_install_command("export-cmd",   shell_cmd_export_cmd, "set variable to result of command: export-cmd VAR command...");
	shell_install_command("source",  shell_cmd_source, "run a shell script in the context of this shell");
	shell_install_command("exec",    shell_cmd_exec, "replace shell (or subshell) with command");
	shell_install_command("not",     shell_cmd_not, "invert status of command");
}
