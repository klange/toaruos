#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <toaru/rline.h>
#include <kuroko.h>
#include "chunk.h"
#include "debug.h"
#include "vm.h"
#include "memory.h"


int main(int argc, char * argv[]) {
	int flags = 0;
	int opt;
	while ((opt = getopt(argc, argv, "tdgs")) != -1) {
		switch (opt) {
			case 't':
				flags |= KRK_ENABLE_TRACING;
				break;
			case 'd':
				flags |= KRK_ENABLE_DEBUGGING;
				break;
			case 's':
				flags |= KRK_ENABLE_SCAN_TRACING;
				break;
			case 'g':
				flags |= KRK_ENABLE_STRESS_GC;
				break;
		}
	}

	krk_initVM(flags);

	KrkValue result = INTEGER_VAL(0);

	if (optind == argc) {
		/* Run the repl */
		int exit = 0;

		rline_exit_string="";
		rline_exp_set_syntax("krk");
		//rline_exp_set_shell_commands(shell_commands, shell_commands_len);
		//rline_exp_set_tab_complete_func(tab_complete_func);

		while (!exit) {
			size_t lineCapacity = 8;
			size_t lineCount = 0;
			char ** lines = ALLOCATE(char *, lineCapacity);
			size_t totalData = 0;
			int valid = 1;
			char * allData = NULL;
			int inBlock = 0;
			int blockWidth = 0;

			rline_exp_set_prompts(">>> ", "", 4, 0);

			while (1) {
				/* This would be a nice place for line editing */
				char buf[4096] = {0};

				if (inBlock) {
					rline_exp_set_prompts("  > ", "", 4, 0);
					rline_preload = malloc(blockWidth + 1);
					for (int i = 0; i < blockWidth; ++i) {
						rline_preload[i] = ' ';
					}
					rline_preload[blockWidth] = '\0';
				}

				rline_scroll = 0;
				if (rline(buf, 4096) == 0) {
					valid = 0;
					exit = 1;
					break;
				}
				if (buf[strlen(buf)-1] != '\n') {
					fprintf(stderr, "Expected end of line in repl input. Did you ^D early?\n");
					valid = 0;
					break;
				}
				if (lineCapacity < lineCount + 1) {
					size_t old = lineCapacity;
					lineCapacity = GROW_CAPACITY(old);
					lines = GROW_ARRAY(char *,lines,old,lineCapacity);
				}

				int i = lineCount++;
				lines[i] = strdup(buf);

				size_t lineLength = strlen(lines[i]);
				totalData += lineLength;

				int is_spaces = 1;
				int count_spaces = 0;
				for (size_t j = 0; j < lineLength; ++j) {
					if (lines[i][j] != ' ' && lines[i][j] != '\n') {
						is_spaces = 0;
						break;
					}
					count_spaces += 1;
				}

				if (lineLength > 2 && lines[i][lineLength-2] == ':') {
					inBlock = 1;
					blockWidth = count_spaces + 4;
					continue;
				} else if (inBlock && lineLength != 1) {
					if (is_spaces) {
						free(lines[i]);
						totalData -= lineLength;
						lineCount--;
						break;
					}
					blockWidth = count_spaces;
					continue;
				}

				break;
			}

			if (valid) {
				allData = malloc(totalData + 1);
				allData[0] = '\0';
			}
			for (size_t i = 0; i < lineCount; ++i) {
				if (valid) strcat(allData, lines[i]);
				rline_history_insert(strdup(lines[i]));
				free(lines[i]);
			}
			FREE_ARRAY(char *, lines, lineCapacity);

			if (valid) {
				KrkValue result = krk_interpret(allData, 0, "<module>","<stdin>");
				if (!IS_NONE(result)) {
					fprintf(stdout, " \033[1;30m=> ");
					krk_printValue(stdout, result);
					fprintf(stdout, "\033[0m\n");
				}
			}

		}
	} else {

		for (int i = optind; i < argc; ++i) {
			KrkValue out = krk_runfile(argv[i],0,"<module>",argv[i]);
			if (i + 1 == argc) result = out;
		}
	}

	krk_freeVM();

	if (IS_INTEGER(result)) return AS_INTEGER(result);

	return 0;
}
