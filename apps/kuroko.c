/**
 * Kuroko interpreter main executable.
 *
 * Reads lines from stdin with the `rline` library and executes them,
 * or executes scripts from the argument list.
 */
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

#ifdef __toaru__
#include <toaru/rline.h>
#else
#ifndef NO_RLINE
#include "vendor/rline.h"
#endif
#endif

#include <kuroko/kuroko.h>
#include <kuroko/chunk.h>
#include <kuroko/debug.h>
#include <kuroko/vm.h>
#include <kuroko/memory.h>
#include <kuroko/scanner.h>
#include <kuroko/compiler.h>
#include <kuroko/util.h>

#define PROMPT_MAIN  ">>> "
#define PROMPT_BLOCK "  > "

#define CALLGRIND_TMP_FILE "/tmp/kuroko.callgrind.tmp"

static int enableRline = 1;
static int exitRepl = 0;
static int pasteEnabled = 0;

KRK_FUNC(exit,{
	FUNCTION_TAKES_NONE();
	exitRepl = 1;
})

KRK_FUNC(paste,{
	FUNCTION_TAKES_AT_MOST(1);
	if (argc) {
		CHECK_ARG(0,bool,int,enabled);
		pasteEnabled = enabled;
	} else {
		pasteEnabled = !pasteEnabled;
	}
	fprintf(stderr, "Pasting is %s.\n", pasteEnabled ? "enabled" : "disabled");
})

static int doRead(char * buf, size_t bufSize) {
#ifndef NO_RLINE
	if (enableRline)
		return rline(buf, bufSize);
	else
#endif
		return read(STDIN_FILENO, buf, bufSize);
}

static KrkValue readLine(char * prompt, int promptWidth, char * syntaxHighlighter) {
	struct StringBuilder sb = {0};

#ifndef NO_RLINE
	if (enableRline) {
		rline_exit_string = "";
		rline_exp_set_prompts(prompt, "", promptWidth, 0);
		rline_exp_set_syntax(syntaxHighlighter);
		rline_exp_set_tab_complete_func(NULL);
	} else
#endif
	{
		fprintf(stdout, "%s", prompt);
		fflush(stdout);
	}

	/* Read a line of input using a method that we can guarantee will be
	 * interrupted by signal delivery. */
	while (1) {
		char buf[4096];
		ssize_t bytesRead = doRead(buf, 4096);
		if (krk_currentThread.flags & KRK_THREAD_SIGNALLED) goto _exit;
		if (bytesRead < 0) {
			krk_runtimeError(vm.exceptions->ioError, "%s", strerror(errno));
			goto _exit;
		} else if (bytesRead == 0 && !sb.length) {
			krk_runtimeError(vm.exceptions->baseException, "EOF");
			goto _exit;
		} else {
			pushStringBuilderStr(&sb, buf, bytesRead);
		}
		/* Was there a linefeed? Then we can exit. */
		if (sb.length && sb.bytes[sb.length-1] == '\n') {
			sb.length--;
			break;
		}
	}

	return finishStringBuilder(&sb);

_exit:
	discardStringBuilder(&sb);
	return NONE_VAL();
}

/**
 * @brief Read a line of input.
 *
 * In an interactive session, presents the configured prompt without
 * a trailing linefeed.
 */
KRK_FUNC(input,{
	FUNCTION_TAKES_AT_MOST(1);

	char * prompt = "";
	int promptLength = 0;
	char * syntaxHighlighter = NULL;

	if (argc) {
		CHECK_ARG(0,str,KrkString*,_prompt);
		prompt = _prompt->chars;
		promptLength = _prompt->codesLength;
	}

	if (hasKw) {
		KrkValue promptwidth;
		if (krk_tableGet(AS_DICT(argv[argc]), OBJECT_VAL(S("promptwidth")), &promptwidth)) {
			if (!IS_INTEGER(promptwidth)) return TYPE_ERROR(int,promptwidth);
			promptLength = AS_INTEGER(promptwidth);
		}

		KrkValue syntax;
		if (krk_tableGet(AS_DICT(argv[argc]), OBJECT_VAL(S("syntax")), &syntax)) {
			if (!IS_STRING(syntax)) return TYPE_ERROR(str,syntax);
			syntaxHighlighter = AS_CSTRING(syntax);
		}
	}

	return readLine(prompt, promptLength, syntaxHighlighter);
})

#ifndef NO_RLINE
/**
 * Given an object, find a property with the same name as a scanner token.
 * This can be either a field of an instance, or a method of the type of
 * the of the object. If we can't find anything by that name, return None.
 *
 * We can probably also use valueGetProperty which does correct binding
 * for native dynamic fields...
 */
static KrkValue findFromProperty(KrkValue current, KrkToken next) {
	KrkValue value;
	KrkValue member = OBJECT_VAL(krk_copyString(next.start, next.literalWidth));
	krk_push(member);

	if (IS_INSTANCE(current)) {
		/* try fields */
		if (krk_tableGet(&AS_INSTANCE(current)->fields, member, &value)) goto _found;
		if (krk_tableGet(&AS_INSTANCE(current)->_class->methods, member, &value)) goto _found;
	} else {
		/* try methods */
		KrkClass * _class = krk_getType(current);
		if (krk_tableGet(&_class->methods, member, &value)) goto _found;
	}

	krk_pop();
	return NONE_VAL();

_found:
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
						(IS_NATIVE(thisValue) && !((KrkNative*)AS_OBJECT(thisValue))->flags & KRK_NATIVE_FLAGS_IS_DYNAMIC_PROPERTY)) {
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
#endif

#ifdef DEBUG
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
#ifndef NO_RLINE
		if (enableRline) {
			rline_exit_string="";
			rline_exp_set_prompts("(dbg) ", "", 6, 0);
			rline_exp_set_syntax("krk-dbg");
			rline_exp_set_tab_complete_func(NULL);
			if (rline(buf, 4096) == 0) goto _dbgQuit;
		} else {
#endif
			fprintf(stderr, "(dbg) ");
			fflush(stderr);
			char * out = fgets(buf, 4096, stdin);
			if (!out || !strlen(buf)) {
				fprintf(stdout, "^D\n");
				goto _dbgQuit;
			}
#ifndef NO_RLINE
		}
#endif

		char * nl = strstr(buf,"\n");
		if (nl) *nl = '\0';

		if (!strlen(buf)) {
			if (lastDebugCommand) {
				strcpy(buf, lastDebugCommand);
			} else {
				continue;
			}
		} else {
#ifndef NO_RLINE
			if (enableRline) {
				rline_history_insert(strdup(buf));
				rline_scroll = 0;
			}
#endif
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
#endif

static void handleSigint(int sigNum) {
	/* Don't set the signal flag if the VM is not running */
	if (!krk_currentThread.frameCount) return;
	krk_currentThread.flags |= KRK_THREAD_SIGNALLED;
}

static void handleSigtrap(int sigNum) {
	if (!krk_currentThread.frameCount) return;
	krk_currentThread.flags |= KRK_THREAD_SINGLE_STEP;
}

static void bindSignalHandlers(void) {
	signal(SIGINT, handleSigint);
	signal(SIGTRAP, handleSigtrap);
}

static void findInterpreter(char * argv[]) {
#ifdef _WIN32
	vm.binpath = strdup(_pgmptr);
#else
	/* Try asking /proc */
	char tmp[4096];
	char * binpath = realpath("/proc/self/exe", tmp);
	if (!binpath || (access(binpath, X_OK) != 0)) {
		if (strchr(argv[0], '/')) {
			binpath = realpath(argv[0], tmp);
		} else {
			/* Search PATH for argv[0] */
			char * _path = strdup(getenv("PATH"));
			char * path = _path;
			while (path) {
				char * next = strchr(path,':');
				if (next) *next++ = '\0';

				snprintf(tmp, 4096, "%s/%s", path, argv[0]);
				if (access(tmp, X_OK) == 0) {
					binpath = tmp;
					break;
				}
				path = next;
			}
			free(_path);
		}
	}
	if (binpath) {
		vm.binpath = strdup(binpath);
	} /* Else, give up at this point and just don't attach it at all. */
#endif
}

static int runString(char * argv[], int flags, char * string) {
	findInterpreter(argv);
	krk_initVM(flags);
	krk_startModule("__main__");
	krk_attachNamedValue(&krk_currentThread.module->fields,"__doc__", NONE_VAL());
	krk_interpret(string, "<stdin>");
	krk_freeVM();
	return 0;
}

static int compileFile(char * argv[], int flags, char * fileName) {
	findInterpreter(argv);
	krk_initVM(flags);

	/* Open the file. */
	FILE * f = fopen(fileName,"r");
	if (!f) {
		fprintf(stderr, "%s: could not read file '%s': %s\n", argv[0], fileName, strerror(errno));
		return 1;
	}

	/* Read it like we normally do... */
	fseek(f, 0, SEEK_END);
	size_t size = ftell(f);
	fseek(f, 0, SEEK_SET);
	char * buf = malloc(size+1);
	if (fread(buf, 1, size, f) != size) return 2;
	fclose(f);
	buf[size] = '\0';

	/* Set up a module scope */
	krk_startModule("__main__");

	/* Call the compiler directly. */
	KrkCodeObject * func = krk_compile(buf, fileName);

	/* See if there was an exception. */
	if (krk_currentThread.flags & KRK_THREAD_HAS_EXCEPTION) {
		krk_dumpTraceback();
	}

	/* Free the source string */
	free(buf);

	/* Close out the compiler */
	krk_freeVM();

	return func == NULL;
}

#ifdef BUNDLE_LIBS
#define BUNDLED(name) do { \
	extern KrkValue krk_module_onload_ ## name (); \
	KrkValue moduleOut = krk_module_onload_ ## name (); \
	krk_attachNamedValue(&vm.modules, # name, moduleOut); \
	krk_attachNamedObject(&AS_INSTANCE(moduleOut)->fields, "__name__", (KrkObj*)krk_copyString(#name, sizeof(#name)-1)); \
	krk_attachNamedValue(&AS_INSTANCE(moduleOut)->fields, "__file__", NONE_VAL()); \
} while (0)
#endif

int main(int argc, char * argv[]) {
#ifdef _WIN32
	SetConsoleOutputCP(65001);
	SetConsoleCP(65001);
#endif
	char * runCmd = NULL;
	int flags = 0;
	int moduleAsMain = 0;
	int inspectAfter = 0;
	int opt;
	while ((opt = getopt(argc, argv, "+:c:C:dgGim:rstTMSV-:")) != -1) {
		switch (opt) {
			case 'c':
				runCmd = optarg;
				goto _finishArgs;
			case 'd':
				/* Disassemble code blocks after compilation. */
				flags |= KRK_THREAD_ENABLE_DISASSEMBLY;
				break;
			case 'g':
				/* Always garbage collect during an allocation. */
				flags |= KRK_GLOBAL_ENABLE_STRESS_GC;
				break;
			case 'G':
				flags |= KRK_GLOBAL_REPORT_GC_COLLECTS;
				break;
			case 's':
				/* Print debug information during compilation. */
				flags |= KRK_THREAD_ENABLE_SCAN_TRACING;
				break;
			case 'S':
				flags |= KRK_THREAD_SINGLE_STEP;
				break;
			case 't':
				/* Disassemble instructions as they are executed. */
				flags |= KRK_THREAD_ENABLE_TRACING;
				break;
			case 'T': {
				flags |= KRK_GLOBAL_CALLGRIND;
				vm.callgrindFile = fopen(CALLGRIND_TMP_FILE,"w");
				break;
			}
			case 'i':
				inspectAfter = 1;
				break;
			case 'm':
				moduleAsMain = 1;
				optind--; /* to get us back to optarg */
				goto _finishArgs;
			case 'r':
				enableRline = 0;
				break;
			case 'M':
				return runString(argv,0,"import kuroko; print(kuroko.module_paths)\n");
			case 'V':
				return runString(argv,0,"import kuroko; print('Kuroko',kuroko.version)\n");
			case 'C':
				return compileFile(argv,flags,optarg);
			case ':':
				fprintf(stderr, "%s: option '%c' requires an argument\n", argv[0], optopt);
				return 1;
			case '?':
				if (optopt != '-') {
					fprintf(stderr, "%s: unrecognized option '%c'\n", argv[0], optopt);
					return 1;
				}
				optarg = argv[optind]+2;
				/* fall through */
			case '-':
				if (!strcmp(optarg,"version")) {
					return runString(argv,0,"import kuroko; print('Kuroko',kuroko.version)\n");
				} else if (!strcmp(optarg,"help")) {
#ifndef KRK_NO_DOCUMENTATION
					fprintf(stderr,"usage: %s [flags] [FILE...]\n"
						"\n"
						"Interpreter options:\n"
						" -d          Debug output from the bytecode compiler.\n"
						" -g          Collect garbage on every allocation.\n"
						" -G          Report GC collections.\n"
						" -i          Enter repl after a running -c, -m, or FILE.\n"
						" -m mod      Run a module as a script.\n"
						" -r          Disable complex line editing in the REPL.\n"
						" -s          Debug output from the scanner/tokenizer.\n"
						" -t          Disassemble instructions as they are exceuted.\n"
						" -T          Write call trace file.\n"
						" -C file     Compile 'file', but do not execute it.\n"
						" -M          Print the default module import paths.\n"
						" -S          Enable single-step debugging.\n"
						" -V          Print version information.\n"
						"\n"
						" --version   Print version information.\n"
						" --help      Show this help text.\n"
						"\n"
						"If no files are provided, the interactive REPL will run.\n",
						argv[0]);
#endif
					return 0;
				} else {
					fprintf(stderr,"%s: unrecognized option '--%s'\n",
						argv[0], optarg);
					return 1;
				}
		}
	}

_finishArgs:
	findInterpreter(argv);
	krk_initVM(flags);

#ifdef DEBUG
	krk_debug_registerCallback(debuggerHook);
#endif

	/* Attach kuroko.argv - argv[0] will be set to an empty string for the repl */
	if (argc == optind) krk_push(OBJECT_VAL(krk_copyString("",0)));
	for (int arg = optind; arg < argc; ++arg) {
		krk_push(OBJECT_VAL(krk_copyString(argv[arg],strlen(argv[arg]))));
	}
	KrkValue argList = krk_list_of(argc - optind + (optind == argc), &krk_currentThread.stackTop[-(argc - optind + (optind == argc))],0);
	krk_push(argList);
	krk_attachNamedValue(&vm.system->fields, "argv", argList);
	krk_pop();
	for (int arg = optind; arg < argc + (optind == argc); ++arg) krk_pop();

	/* Bind interrupt signal */
	bindSignalHandlers();

#ifdef BUNDLE_LIBS
	/* Add any other modules you want to include that are normally built as shared objects. */
	BUNDLED(math);
	BUNDLED(socket);
#endif

	KrkValue result = INTEGER_VAL(0);

	/**
	 * Add general builtins that aren't part of the core VM.
	 * This is where we provide @c input in particular.
	 */
	KRK_DOC(BIND_FUNC(vm.builtins,input), "@brief Read a line of input.\n"
		"@arguments [prompt], promptwidth=None, syntax=None\n\n"
		"Read a line of input from @c stdin. If the @c rline library is available, "
		"it will be used to gather input. Input reading stops on end-of file or when "
		"a read ends with a line feed, which will be removed from the returned string. "
		"If a prompt is provided, it will be printed without a line feed before requesting "
		"input. If @c rline is available, the prompt will be passed to the library as the "
		"left-hand prompt string. If not provided, @p promptwidth will default to the width "
		"of @p prompt in codepoints; if you are providing a prompt with escape characters or "
		"characters with multi-column East-Asian Character Width be sure to pass a value "
		"for @p promptwidth that reflects the display width of your prompt. "
		"If provided, @p syntax specifies the name of an @c rline syntax module to "
		"provide color highlighting of the input line.");

	if (moduleAsMain) {
		krk_push(OBJECT_VAL(krk_copyString("__main__",8)));
		int out = !krk_importModule(
			AS_STRING(AS_LIST(argList)->values[0]),
			AS_STRING(krk_peek(0)));
		if (krk_currentThread.flags & KRK_THREAD_HAS_EXCEPTION) {
			krk_dumpTraceback();
			krk_resetStack();
		}
		if (!inspectAfter) return out;
		if (IS_INSTANCE(krk_peek(0))) {
			krk_currentThread.module = AS_INSTANCE(krk_peek(0));
		}
	} else if (optind != argc) {
		krk_startModule("__main__");
		result = krk_runfile(argv[optind],argv[optind]);
		if (IS_NONE(result) && krk_currentThread.flags & KRK_THREAD_HAS_EXCEPTION) result = INTEGER_VAL(1);
	}

	if (!krk_currentThread.module) {
		/* The repl runs in the context of a top-level module so each input
		 * line can share a globals state with the others. */
		krk_startModule("__main__");
		krk_attachNamedValue(&krk_currentThread.module->fields,"__doc__", NONE_VAL());
	}

	if (runCmd) {
		result = krk_interpret(runCmd, "<stdin>");
	}

	if ((!moduleAsMain && !runCmd && optind == argc) || inspectAfter) {
		/* Add builtins for the repl, but hide them from the globals() list. */
		KRK_DOC(BIND_FUNC(vm.builtins,exit), "@brief Exit the interactive repl.\n\n"
			"Only available from the interactive interpreter; exits the repl.");
		KRK_DOC(BIND_FUNC(vm.builtins,paste), "@brief Toggle paste mode.\n"
			"@arguments enabled=None\n\n"
			"Toggles paste-safe mode, disabling automatic indentation in the repl. "
			"If @p enabled is specified, the mode can be directly specified, otherwise "
			"it will be set to the opposite of the current mode. The new mode will be "
			"printed to stderr.");

		/**
		 * Python stores version info in a built-in module called `sys`.
		 * We are not Python, we'll use `sys` to pretend to be Python
		 * in emulation mode, so we use a different module to store
		 * this sort of thing: kuroko
		 *
		 * This module won't be imported by default, but it's still in
		 * the modules list, so we can look for it there.
		 */
		KrkValue systemModule;
		if (krk_tableGet(&vm.modules, OBJECT_VAL(krk_copyString("kuroko",6)), &systemModule)) {
			KrkValue version, buildenv, builddate;
			krk_tableGet(&AS_INSTANCE(systemModule)->fields, OBJECT_VAL(krk_copyString("version",7)), &version);
			krk_tableGet(&AS_INSTANCE(systemModule)->fields, OBJECT_VAL(krk_copyString("buildenv",8)), &buildenv);
			krk_tableGet(&AS_INSTANCE(systemModule)->fields, OBJECT_VAL(krk_copyString("builddate",9)), &builddate);

			fprintf(stdout, "Kuroko %s (%s) with %s\n",
				AS_CSTRING(version), AS_CSTRING(builddate), AS_CSTRING(buildenv));
		}

		fprintf(stdout, "Type `help` for guidance, `paste()` to toggle automatic indentation, `license` for copyright information.\n");

		while (!exitRepl) {
			size_t lineCapacity = 8;
			size_t lineCount = 0;
			char ** lines = ALLOCATE(char *, lineCapacity);
			size_t totalData = 0;
			int valid = 1;
			char * allData = NULL;
			int inBlock = 0;
			int blockWidth = 0;

#ifndef NO_RLINE
			/* Main prompt is >>> like in Python */
			rline_exp_set_prompts(PROMPT_MAIN, "", 4, 0);
			/* Set ^D to send EOF */
			rline_exit_string="";
			/* Enable syntax highlight for Kuroko */
			rline_exp_set_syntax("krk");
			/* Bind a callback for \t */
			rline_exp_set_tab_complete_func(tab_complete_func);
#endif

			while (1) {
				/* This would be a nice place for line editing */
				char buf[4096] = {0};

#ifndef NO_RLINE
				if (inBlock) {
					/* When entering multiple lines, the additional lines
					 * will show a single > (and keep the left side aligned) */
					rline_exp_set_prompts(PROMPT_BLOCK, "", 4, 0);
					/* Also add indentation as necessary */
					if (!pasteEnabled) {
						rline_preload = malloc(blockWidth + 1);
						for (int i = 0; i < blockWidth; ++i) {
							rline_preload[i] = ' ';
						}
						rline_preload[blockWidth] = '\0';
					}
				}

				if (!enableRline) {
#else
				if (1) {
#endif
					fprintf(stdout, "%s", inBlock ? PROMPT_BLOCK : PROMPT_MAIN);
					fflush(stdout);
				}

#ifndef NO_RLINE
				rline_scroll = 0;
				if (enableRline) {
					if (rline(buf, 4096) == 0) {
						valid = 0;
						exitRepl = 1;
						break;
					}
				} else {
#endif
					char * out = fgets(buf, 4096, stdin);
					if (!out || !strlen(buf)) {
						fprintf(stdout, "^D\n");
						valid = 0;
						exitRepl = 1;
						break;
					}
#ifndef NO_RLINE
				}
#endif

				if (buf[strlen(buf)-1] != '\n') {
					/* rline shouldn't allow this as it doesn't accept ^D to submit input
					 * unless the line is empty, but just in case... */
					fprintf(stderr, "Expected end of line in repl input. Did you ^D early?\n");
					valid = 0;
					break;
				}

				if (lineCapacity < lineCount + 1) {
					/* If we need more space, grow as needed... */
					size_t old = lineCapacity;
					lineCapacity = GROW_CAPACITY(old);
					lines = GROW_ARRAY(char *,lines,old,lineCapacity);
				}

				int i = lineCount++;
				lines[i] = strdup(buf);

				size_t lineLength = strlen(lines[i]);
				totalData += lineLength;

				/* Figure out indentation */
				int isSpaces = 1;
				int countSpaces = 0;
				for (size_t j = 0; j < lineLength; ++j) {
					if (lines[i][j] != ' ' && lines[i][j] != '\n') {
						isSpaces = 0;
						break;
					}
					countSpaces += 1;
				}

				/* Naively detect the start of a new block so we can
				 * continue to accept input. Our compiler isn't really
				 * set up to let us compile "on the fly" so we can't just
				 * run lines through it and see if it wants more... */
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

				/* Ignore blank lines. */
				if (isSpaces && !i) valid = 0;

				/* If we're not in a block, or have entered a blank line,
				 * we can stop reading new lines and jump to execution. */
				break;
			}

			if (valid) {
				allData = malloc(totalData + 1);
				allData[0] = '\0';
			}

			for (size_t i = 0; i < lineCount; ++i) {
				if (valid) strcat(allData, lines[i]);
#ifndef NO_RLINE
				if (enableRline) {
					rline_history_insert(strdup(lines[i]));
					rline_scroll = 0;
				}
#endif
				free(lines[i]);
			}
			FREE_ARRAY(char *, lines, lineCapacity);

			if (valid) {
				KrkValue result = krk_interpret(allData, "<stdin>");
				if (!IS_NONE(result)) {
					KrkClass * type = krk_getType(result);
					const char * formatStr = " \033[1;30m=> %s\033[0m\n";
					if (type->_reprer) {
						krk_push(result);
						result = krk_callSimple(OBJECT_VAL(type->_reprer), 1, 0);
					} else if (type->_tostr) {
						krk_push(result);
						result = krk_callSimple(OBJECT_VAL(type->_tostr), 1, 0);
					}
					if (!IS_STRING(result)) {
						fprintf(stdout, " \033[1;31m=> Unable to produce representation for value.\033[0m\n");
					} else {
						fprintf(stdout, formatStr, AS_CSTRING(result));
					}
				}
				krk_resetStack();
				free(allData);
			}

			(void)blockWidth;
		}
	}

	if (vm.globalFlags & KRK_GLOBAL_CALLGRIND) {
		fclose(vm.callgrindFile);
		vm.globalFlags &= ~(KRK_GLOBAL_CALLGRIND);

		krk_resetStack();
		krk_startModule("<callgrind>");
		krk_attachNamedObject(&krk_currentThread.module->fields, "filename", (KrkObj*)S(CALLGRIND_TMP_FILE));
		krk_interpret(
			"from callgrind import processFile\n"
			"import kuroko\n"
			"import os\n"
			"processFile(filename, os.getpid(), ' '.join(kuroko.argv))","<callgrind>");
	}

	krk_freeVM();

	if (IS_INTEGER(result)) return AS_INTEGER(result);

	return 0;
}
