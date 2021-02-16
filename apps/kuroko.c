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
#include <kuroko.h>
#else
#ifndef NO_RLINE
#include "rline.h"
#endif
#include "kuroko.h"
#endif

#include "chunk.h"
#include "debug.h"
#include "vm.h"
#include "memory.h"
#include "scanner.h"

#define PROMPT_MAIN  ">>> "
#define PROMPT_BLOCK "  > "

static int enableRline = 1;
static int exitRepl = 0;
static KrkValue exitFunc(int argc, KrkValue argv[], int hasKw) {
	exitRepl = 1;
	return NONE_VAL();
}

static int pasteEnabled = 0;
static KrkValue paste(int argc, KrkValue argv[], int hasKw) {
	pasteEnabled = !pasteEnabled;
	fprintf(stderr, "Pasting is %s.\n", pasteEnabled ? "enabled" : "disabled");
	return NONE_VAL();
}

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
						(IS_NATIVE(thisValue) && ((KrkNative*)AS_OBJECT(thisValue))->isMethod != 2)) {
						char * tmp = malloc(s->length + 2);
						sprintf(tmp, "%s(", s->chars);
						s = krk_takeString(tmp, strlen(tmp));
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

static void handleSigint(int sigNum) {
	if (krk_currentThread.frameCount) {
		krk_runtimeError(vm.exceptions->keyboardInterrupt, "Keyboard interrupt.");
	}

	signal(sigNum, handleSigint);
}

static void findInterpreter(char * argv[]) {
#ifdef _WIN32
	vm.binpath = strdup(_pgmptr);
#else
	/* Try asking /proc */
	char * binpath = realpath("/proc/self/exe", NULL);
	if (!binpath || (access(binpath, X_OK) != 0)) {
		if (strchr(argv[0], '/')) {
			binpath = realpath(argv[0], NULL);
		} else {
			/* Search PATH for argv[0] */
			char * _path = strdup(getenv("PATH"));
			char * path = _path;
			while (path) {
				char * next = strchr(path,':');
				if (next) *next++ = '\0';

				char tmp[4096];
				sprintf(tmp, "%s/%s", path, argv[0]);
				if (access(tmp, X_OK) == 0) {
					binpath = strdup(tmp);
					break;
				}
				path = next;
			}
			free(_path);
		}
	}
	if (binpath) {
		vm.binpath = binpath;
	} /* Else, give up at this point and just don't attach it at all. */
#endif
}

static int runString(char * argv[], int flags, char * string) {
	findInterpreter(argv);
	krk_initVM(flags);
	krk_startModule("__main__");
	krk_interpret(string, 1, "<stdin>","<stdin>");
	krk_freeVM();
	return 0;
}

/* This isn't normally exposed. */
extern KrkFunction * krk_compile(const char * src, int newScope, char * fileName);
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
	KrkFunction * func = krk_compile(buf, 0, fileName);

	/* See if there was an exception. */
	if (krk_currentThread.flags & KRK_HAS_EXCEPTION) {
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
	int flags = 0;
	int moduleAsMain = 0;
	int opt;
	while ((opt = getopt(argc, argv, "+c:C:dgm:rstMV-:")) != -1) {
		switch (opt) {
			case 'c':
				return runString(argv, flags, optarg);
			case 'd':
				/* Disassemble code blocks after compilation. */
				flags |= KRK_ENABLE_DISASSEMBLY;
				break;
			case 'g':
				/* Always garbage collect during an allocation. */
				flags |= KRK_ENABLE_STRESS_GC;
				break;
			case 's':
				/* Print debug information during compilation. */
				flags |= KRK_ENABLE_SCAN_TRACING;
				break;
			case 't':
				/* Disassemble instructions as they are executed. */
				flags |= KRK_ENABLE_TRACING;
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
			case '-':
				if (!strcmp(optarg,"version")) {
					return runString(argv,0,"import kuroko; print('Kuroko',kuroko.version)\n");
				} else if (!strcmp(optarg,"help")) {
					fprintf(stderr,"usage: %s [flags] [FILE...]\n"
						"\n"
						"Interpreter options:\n"
						" -d          Debug output from the bytecode compiler.\n"
						" -g          Collect garbage on every allocation.\n"
						" -m mod      Run a module as a script.\n"
						" -r          Disable complex line editing in the REPL.\n"
						" -s          Debug output from the scanner/tokenizer.\n"
						" -t          Disassemble instructions as they are exceuted.\n"
						" -C file     Compile 'file', but do not execute it.\n"
						" -M          Print the default module import paths.\n"
						" -V          Print version information.\n"
						"\n"
						" --version   Print version information.\n"
						" --help      Show this help text.\n"
						"\n"
						"If no files are provided, the interactive REPL will run.\n",
						argv[0]);
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
	signal(SIGINT, handleSigint);

#ifdef BUNDLE_LIBS
	/* Add any other modules you want to include that are normally built as shared objects. */
	BUNDLED(fileio);
	BUNDLED(dis);
	BUNDLED(os);
	BUNDLED(time);
	BUNDLED(math);
#endif

	KrkValue result = INTEGER_VAL(0);

	if (moduleAsMain) {
		/* This isn't going to do what we want for built-in modules, but I'm not sure
		 * what we _should_ do for them anyway... let's just leave that as a TODO;
		 * we do let C modules know they are the __main__ now, so non-built-in
		 * C modules can still act as scripts if they want... */
		KrkValue module;
		krk_push(OBJECT_VAL(krk_copyString("__main__",8)));
		int out = !krk_loadModule(
			AS_STRING(AS_LIST(argList)->values[0]),
			&module,
			AS_STRING(krk_peek(0)));
		if (krk_currentThread.flags & KRK_HAS_EXCEPTION) {
			krk_dumpTraceback();
			krk_resetStack();
		}
		return out;
	} else if (optind == argc) {
		/* Add builtins for the repl, but hide them from the globals() list. */
		krk_defineNative(&vm.builtins->fields, "exit", exitFunc);
		krk_defineNative(&vm.builtins->fields, "paste", paste);

		/* The repl runs in the context of a top-level module so each input
		 * line can share a globals state with the others. */
		krk_startModule("<module>");
		krk_attachNamedValue(&krk_currentThread.module->fields,"__doc__", NONE_VAL());

#ifndef NO_RLINE
		/* Set ^D to send EOF */
		rline_exit_string="";
		/* Enable syntax highlight for Kuroko */
		rline_exp_set_syntax("krk");
		/* Bind a callback for \t */
		rline_exp_set_tab_complete_func(tab_complete_func);
#endif

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
				if (enableRline) rline_history_insert(strdup(lines[i]));
#endif
				free(lines[i]);
			}
			FREE_ARRAY(char *, lines, lineCapacity);

			if (valid) {
				KrkValue result = krk_interpret(allData, 0, "<module>","<stdin>");
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
					krk_resetStack();
				}
				free(allData);
			}

			(void)blockWidth;
		}
	} else {
		krk_startModule("__main__");
		result = krk_runfile(argv[optind],1,"__main__",argv[optind]);
	}

	krk_freeVM();

	if (IS_INTEGER(result)) return AS_INTEGER(result);

	return 0;
}
