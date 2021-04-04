#include <string.h>
#include <stdlib.h>
#include <kuroko/kuroko.h>
#include <kuroko/chunk.h>
#include <kuroko/debug.h>
#include <kuroko/vm.h>
#include <kuroko/memory.h>
#include <kuroko/scanner.h>
#include <kuroko/compiler.h>
#include <kuroko/util.h>
#include "rline.h"

#define PROMPT_MAIN  ">>> "
#define PROMPT_BLOCK "  > "

extern void krk_printResult(unsigned long long val);

static int exitRepl = 0;

static KrkValue findFromProperty(KrkValue current, KrkToken next) {
	KrkValue member = OBJECT_VAL(krk_copyString(next.start, next.literalWidth));
	krk_push(member);
	KrkValue value = krk_valueGetAttribute_default(current, AS_CSTRING(member), NONE_VAL());
	krk_pop();
	return value;
}

static void tab_complete_func(rline_context_t * c) {
	/* Figure out where the cursor is and if we should be completing anything. */
	if (c->offset) {
		/* Copy up to the cursor... */
		char * tmp = malloc(c->offset + 1);
		memcpy(tmp, c->buffer, c->offset);
		tmp[c->offset] = '\0';
		/* and pass it to the scanner... */
		krk_initScanner(tmp);
		/* Logically, there can be at most (offset) tokens, plus some wiggle room. */
		KrkToken * space = malloc(sizeof(KrkToken) * (c->offset + 2));
		int count = 0;
		do {
			space[count++] = krk_scanToken();
		} while (space[count-1].type != TOKEN_EOF && space[count-1].type != TOKEN_ERROR);

		/* If count == 1, it was EOF or an error and we have nothing to complete. */
		if (count == 1) {
			goto _cleanup;
		}

		/* Otherwise we want to see if we're on an identifier or a dot. */
		int base = 2;
		int n = base;
		if (space[count-base].type == TOKEN_DOT) {
			/* Dots we need to look back at the previous tokens for */
			n--;
			base--;
		} else if (space[count-base].type >= TOKEN_IDENTIFIER && space[count-base].type <= TOKEN_WITH) {
			/* Something alphanumeric; only for the last element */
		} else {
			/* Some other symbol */
			goto _cleanup;
		}

		/* Work backwards to find the start of this chain of identifiers */
		while (n < count) {
			if (space[count-n-1].type != TOKEN_DOT) break;
			n++;
			if (n == count) break;
			if (space[count-n-1].type != TOKEN_IDENTIFIER) break;
			n++;
		}

		if (n <= count) {
			/* Now work forwards, starting from the current globals. */
			KrkValue root = OBJECT_VAL(krk_currentThread.module);
			int isGlobal = 1;
			while (n > base) {
				/* And look at the potential fields for instances/classes */
				KrkValue next = findFromProperty(root, space[count-n]);
				if (IS_NONE(next)) {
					/* If we hit None, we found something invalid (or literally hit a None
					 * object, but really the difference is minimal in this case: Nothing
					 * useful to tab complete from here. */
					goto _cleanup;
				}
				isGlobal = 0;
				root = next;
				n -= 2; /* To skip every other dot. */
			}

			/* Now figure out what we're completing - did we already have a partial symbol name? */
			int length = (space[count-base].type == TOKEN_DOT) ? 0 : (space[count-base].length);
			isGlobal = isGlobal && (length != 0);

			/* Collect up to 256 of those that match */
			char * matches[256];
			int matchCount = 0;

			/* Take the last symbol name from the chain and get its member list from dir() */

			for (;;) {
				KrkValue dirList = krk_dirObject(1,(KrkValue[]){root},0);
				krk_push(dirList);
				if (!IS_INSTANCE(dirList)) {
					fprintf(stderr,"\nInternal error while tab completting.\n");
					goto _cleanup;
				}

				for (size_t i = 0; i < AS_LIST(dirList)->count; ++i) {
					KrkString * s = AS_STRING(AS_LIST(dirList)->values[i]);
					krk_push(OBJECT_VAL(s));
					KrkToken asToken = {.start = s->chars, .literalWidth = s->length};
					KrkValue thisValue = findFromProperty(root, asToken);
					krk_push(thisValue);
					if (IS_CLOSURE(thisValue) || IS_BOUND_METHOD(thisValue) ||
						(IS_NATIVE(thisValue) && !(((KrkNative*)AS_OBJECT(thisValue))->flags & KRK_NATIVE_FLAGS_IS_DYNAMIC_PROPERTY))) {
						size_t allocSize = s->length + 2;
						char * tmp = malloc(allocSize);
						size_t len = snprintf(tmp, allocSize, "%s(", s->chars);
						s = krk_takeString(tmp, len);
						krk_pop();
						krk_push(OBJECT_VAL(s));
					}

					/* If this symbol is shorter than the current submatch, skip it. */
					if (length && (int)s->length < length) continue;

					/* See if it's already in the matches */
					int found = 0;
					for (int i = 0; i < matchCount; ++i) {
						if (!strcmp(matches[i], s->chars)) {
							found = 1;
							break;
						}
					}
					if (found) continue;

					if (!memcmp(s->chars, space[count-base].start, length)) {
						matches[matchCount] = s->chars;
						matchCount++;
						if (matchCount == 255) goto _toomany;
					}
				}

				/*
				 * If the object we were scanning was the current module,
				 * then we should also throw the builtins into the ring.
				 */
				if (isGlobal && AS_OBJECT(root) == (KrkObj*)krk_currentThread.module) {
					root = OBJECT_VAL(vm.builtins);
					continue;
				} else if (isGlobal && AS_OBJECT(root) == (KrkObj*)vm.builtins) {
					extern char * syn_krk_keywords[];
					KrkInstance * fakeKeywordsObject = krk_newInstance(vm.baseClasses->objectClass);
					root = OBJECT_VAL(fakeKeywordsObject);
					krk_push(root);
					for (char ** keyword = syn_krk_keywords; *keyword; keyword++) {
						krk_attachNamedValue(&fakeKeywordsObject->fields, *keyword, NONE_VAL());
					}
					continue;
				} else {
					break;
				}
			}
_toomany:

			/* Now we can do things with the matches. */
			if (matchCount == 1) {
				/* If there was only one, just fill it. */
				rline_insert(c, matches[0] + length);
				rline_place_cursor();
			} else if (matchCount) {
				/* Otherwise, try to find a common substring among them... */
				int j = length;
				while (1) {
					char m = matches[0][j];
					if (!m) break;
					int diff = 0;
					for (int i = 1; i < matchCount; ++i) {
						if (matches[i][j] != m) {
							diff = 1;
							break;
						}
					}
					if (diff) break;
					j++;
				}
				/* If no common sub string could be filled in, we print the list. */
				if (j == length) {
					/* First find the maximum width of an entry */
					int maxWidth = 0;
					for (int i = 0; i < matchCount; ++i) {
						if ((int)strlen(matches[i]) > maxWidth) maxWidth = strlen(matches[i]);
					}
					/* Now how many can we fit in a screen */
					int colsPerLine = rline_terminal_width / (maxWidth + 2); /* +2 for the spaces */
					fprintf(stderr, "\n");
					int column = 0;
					for (int i = 0; i < matchCount; ++i) {
						fprintf(stderr, "%-*s  ", maxWidth, matches[i]);
						column += 1;
						if (column >= colsPerLine) {
							fprintf(stderr, "\n");
							column = 0;
						}
					}
					if (column != 0) fprintf(stderr, "\n");
				} else {
					/* If we do have a common sub string, fill in those characters. */
					for (int i = length; i < j; ++i) {
						char tmp[2] = {matches[0][i], '\0'};
						rline_insert(c, tmp);
					}
				}
			}
		}
_cleanup:
		free(tmp);
		free(space);
		krk_resetStack();
		return;
	}
}

static char * lastDebugCommand = NULL;
static int debuggerHook(KrkCallFrame * frame) {

	/* File information */
	fprintf(stderr, "At offset 0x%04lx of function '%s' from '%s' on line %lu:\n",
		(unsigned long)(frame->ip - frame->closure->function->chunk.code),
		frame->closure->function->name->chars,
		frame->closure->function->chunk.filename->chars,
		(unsigned long)krk_lineNumber(&frame->closure->function->chunk,
			(unsigned long)(frame->ip - frame->closure->function->chunk.code)));

	/* Opcode trace */
	krk_disassembleInstruction(
		stderr,
		frame->closure->function,
		(size_t)(frame->ip - frame->closure->function->chunk.code));

	krk_debug_dumpStack(stderr, frame);

	while (1) {
		char buf[4096] = {0};
		rline_exit_string="";
		rline_exp_set_prompts("(dbg) ", "", 6, 0);
		rline_exp_set_syntax("krk-dbg");
		rline_exp_set_tab_complete_func(NULL);
		if (rline(buf, 4096) == 0) goto _dbgQuit;

		char * nl = strstr(buf,"\n");
		if (nl) *nl = '\0';

		if (!strlen(buf)) {
			if (lastDebugCommand) {
				strcpy(buf, lastDebugCommand);
			} else {
				continue;
			}
		} else {
			rline_history_insert(strdup(buf));
			rline_scroll = 0;
			if (lastDebugCommand) free(lastDebugCommand);
			lastDebugCommand = strdup(buf);
		}

		/* Try to tokenize the first bit */
		char * arg = NULL;
		char * sp = strstr(buf," ");
		if (sp) {
			*sp = '\0';
			arg = sp + 1;
		}
		/* Now check commands */
		if (!strcmp(buf, "c") || !strcmp(buf,"continue")) {
			return KRK_DEBUGGER_CONTINUE;
		} else if (!strcmp(buf, "s") || !strcmp(buf, "step")) {
			return KRK_DEBUGGER_STEP;
		} else if (!strcmp(buf, "abort")) {
			return KRK_DEBUGGER_ABORT;
		} else if (!strcmp(buf, "q") || !strcmp(buf, "quit")) {
			return KRK_DEBUGGER_QUIT;
		} else if (!strcmp(buf, "bt") || !strcmp(buf, "backtrace")) {
			krk_debug_dumpTraceback();
		} else if (!strcmp(buf, "p") || !strcmp(buf, "print")) {
			if (!arg) {
				fprintf(stderr, "print requires an argument\n");
			} else {
				size_t frameCount = krk_currentThread.frameCount;
				/* Compile statement */
				KrkCodeObject * expression = krk_compile(arg,"<debugger>");
				if (expression) {
					/* Make sure stepping is disabled first. */
					krk_debug_disableSingleStep();
					/* Turn our compiled expression into a callable. */
					krk_push(OBJECT_VAL(expression));
					krk_push(OBJECT_VAL(krk_newClosure(expression)));
					/* Stack silliness, don't ask. */
					krk_push(NONE_VAL());
					krk_pop();
					/* Call the compiled expression with no args, but claim 2 method extras. */
					krk_push(krk_callSimple(krk_peek(0), 0, 2));
					fprintf(stderr, "\033[1;30m=> ");
					krk_printValue(stderr, krk_peek(0));
					fprintf(stderr, "\033[0m\n");
					krk_pop();
				}
				if (krk_currentThread.flags & KRK_THREAD_HAS_EXCEPTION) {
					krk_dumpTraceback();
					krk_currentThread.flags &= ~(KRK_THREAD_HAS_EXCEPTION);
				}
				krk_currentThread.frameCount = frameCount;
			}
		} else if (!strcmp(buf, "break") || !strcmp(buf, "b")) {
			char * filename = arg;
			if (!filename) {
				fprintf(stderr, "usage: break FILE LINE [type]\n");
				continue;
			}

			char * lineno = strstr(filename, " ");
			if (!lineno) {
				fprintf(stderr, "usage: break FILE LINE [type]\n");
				continue;
			}

			/* Advance whitespace */
			*lineno = '\0';
			lineno++;

			/* collect optional type */
			int flags = KRK_BREAKPOINT_NORMAL;
			char * type = strstr(lineno, " ");
			if (type) {
				*type = '\0'; type++;
				if (!strcmp(type, "repeat") || !strcmp(type,"r")) {
					flags = KRK_BREAKPOINT_REPEAT;
				} else if (!strcmp(type, "once") || !strcmp(type,"o")) {
					flags = KRK_BREAKPOINT_ONCE;
				} else {
					fprintf(stderr, "Unrecognized breakpoint type: %s\n", type);
					continue;
				}
			}

			int lineInt = atoi(lineno);
			int result = krk_debug_addBreakpointFileLine(krk_copyString(filename, strlen(filename)), lineInt, flags);

			if (result == -1) {
				fprintf(stderr, "Sorry, couldn't add breakpoint.\n");
			} else {
				fprintf(stderr, "Breakpoint %d enabled.\n", result);
			}

		} else if (!strcmp(buf, "i") || !strcmp(buf, "info")) {
			if (!arg) {
				fprintf(stderr, " info breakpoints - Show breakpoints.\n");
				continue;
			}

			if (!strcmp(arg,"breakpoints")) {
				KrkCodeObject * codeObject = NULL;
				size_t offset = 0;
				int flags = 0;
				int enabled = 0;
				int breakIndex = 0;
				while (1) {
					int result = krk_debug_examineBreakpoint(breakIndex, &codeObject, &offset, &flags, &enabled);
					if (result == -1) break;
					if (result == -2) continue;

					fprintf(stderr, "%-4d in %s+%d %s %s\n",
						breakIndex,
						codeObject->name->chars,
						(int)offset,
						flags == KRK_BREAKPOINT_NORMAL ? "normal":
								flags == KRK_BREAKPOINT_REPEAT ? "repeat" :
									flags == KRK_BREAKPOINT_ONCE ? "once" : "?",
						enabled ? "enabled" : "disabled");

					breakIndex++;
				}
			} else {
				fprintf(stderr, "Unrecognized info object: %s\n", arg);
			}

		} else if (!strcmp(buf, "e") || !strcmp(buf, "enable")) {
			if (!arg) {
				fprintf(stderr, "enable requires an argument\n");
				continue;
			}

			int breakIndex = atoi(arg);

			if (krk_debug_enableBreakpoint(breakIndex)) {
				fprintf(stderr, "Invalid breakpoint handle.\n");
			} else {
				fprintf(stderr, "Breakpoint %d enabled.\n", breakIndex);
			}
		} else if (!strcmp(buf, "d") || !strcmp(buf, "disable")) {
			if (!arg) {
				fprintf(stderr, "disable requires an argument\n");
				continue;
			}

			int breakIndex = atoi(arg);

			if (krk_debug_disableBreakpoint(breakIndex)) {
				fprintf(stderr, "Invalid breakpoint handle.\n");
			} else {
				fprintf(stderr, "Breakpoint %d disabled.\n", breakIndex);
			}
		} else if (!strcmp(buf, "r") || !strcmp(buf, "remove")) {
			if (!arg) {
				fprintf(stderr, "remove requires an argument\n");
				continue;
			}

			int breakIndex = atoi(arg);

			if (krk_debug_removeBreakpoint(breakIndex)) {
				fprintf(stderr, "Invalid breakpoint handle.\n");
			} else {
				fprintf(stderr, "Breakpoint %d removed.\n", breakIndex);
			}
		} else if (!strcmp(buf, "help")) {
			fprintf(stderr,
				"Kuroko Interactive Debugger\n"
				"  c   continue  - Continue until the next breakpoint.\n"
				"  s   step      - Execute this instruction and return to the debugger.\n"
				"  bt  backtrace - Print a backtrace.\n"
				"  q   quit      - Exit the interpreter.\n"
				"      abort     - Abort the interpreter (may create a core dump).\n"
				"  b   break ... - Set a breakpoint.\n"
				"  e   enable N  - Enable breakpoint 'N'.\n"
				"  d   disable N - Disable breakpoint 'N'.\n"
				"  r   remove N  - Remove breakpoint 'N'.\n"
				"  i   info ...  - See information about breakpoints.\n"
				"\n"
				"Empty input lines will repeat the last command.\n"
			);
		} else {
			fprintf(stderr, "Unrecognized command: %s\n", buf);
		}

	}

	return KRK_DEBUGGER_CONTINUE;
_dbgQuit:
	return KRK_DEBUGGER_QUIT;
}

int krk_repl(void) {
	krk_debug_registerCallback(debuggerHook);
	while (!exitRepl) {
		size_t lineCapacity = 8;
		size_t lineCount = 0;
		char ** lines = ALLOCATE(char *, lineCapacity);
		size_t totalData = 0;
		int valid = 1;
		char * allData = NULL;
		int inBlock = 0;
		int blockWidth = 0;
		rline_exp_set_prompts(PROMPT_MAIN, "", 4, 0);
		rline_exit_string="exit";
		rline_exp_set_syntax("krk");
		rline_exp_set_tab_complete_func(tab_complete_func);

		while (1) {
			char buf[4096] = {0};
			if (inBlock) {
				rline_exp_set_prompts(PROMPT_BLOCK, "", 4, 0);
				rline_preload = malloc(blockWidth + 1);
				for (int i = 0; i < blockWidth; ++i) {
					rline_preload[i] = ' ';
				}
				rline_preload[blockWidth] = '\0';
			}
			rline_scroll = 0;
			if (rline(buf, 4096) == 0) {
				valid = 0;
				exitRepl = 1;
				break;
			}
			if (buf[strlen(buf)-1] != '\n') {
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
			int isSpaces = 1;
			int countSpaces = 0;
			for (size_t j = 0; j < lineLength; ++j) {
				if (lines[i][j] != ' ' && lines[i][j] != '\n') {
					isSpaces = 0;
					break;
				}
				countSpaces += 1;
			}
			if (lineLength > 1 && lines[i][lineLength-2] == ':') {
				inBlock = 1;
				blockWidth = countSpaces + 4;
				continue;
			} else if (lineLength > 1 && lines[i][lineLength-2] == '\\') {
				inBlock = 1;
				continue;
			} else if (inBlock && lineLength != 1) {
				if (isSpaces) {
					free(lines[i]);
					totalData -= lineLength;
					lineCount--;
					break;
				}
				blockWidth = countSpaces;
				continue;
			} else if (lineLength > 1 && lines[i][countSpaces] == '@') {
				inBlock = 1;
				blockWidth = countSpaces;
				continue;
			}
			if (isSpaces && !i) valid = 0;
			break;
		}
		if (valid) {
			allData = malloc(totalData + 1);
			allData[0] = '\0';
		}
		for (size_t i = 0; i < lineCount; ++i) {
			if (valid) strcat(allData, lines[i]);
			rline_history_insert(strdup(lines[i]));
			rline_scroll = 0;
			free(lines[i]);
		}
		FREE_ARRAY(char *, lines, lineCapacity);
		if (valid) {
			KrkValue result = krk_interpret(allData, "<stdin>");
			krk_printResult(result);
			krk_resetStack();
		}
		(void)blockWidth;
	}
}

