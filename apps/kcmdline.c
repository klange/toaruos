#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <toaru/hashmap.h>

hashmap_t * args_map = NULL;

int tokenize(char * str, char * sep, char **buf) {
	char * pch_i;
	char * save_i;
	int    argc = 0;
	pch_i = strtok_r(str,sep,&save_i);
	if (!pch_i) { return 0; }
	while (pch_i != NULL) {
		buf[argc] = (char *)pch_i;
		++argc;
		pch_i = strtok_r(NULL,sep,&save_i);
	}
	buf[argc] = NULL;
	return argc;
}

void args_parse(char * _arg) {
	char * arg = strdup(_arg);
	char * argv[1024];
	int argc = tokenize(arg, " ", argv);

	for (int i = 0; i < argc; ++i) {
		char * c = strdup(argv[i]);

		char * name;
		char * value;

		name = c;
		value = NULL;
		/* Find the first = and replace it with a null */
		char * v = c;
		while (*v) {
			if (*v == '=') {
				*v = '\0';
				v++;
				value = v;
				goto _break;
			}
			v++;
		}

_break:
		hashmap_set(args_map, name, value);
	}

	free(arg);
}

void show_usage(int argc, char * argv[]) {
	printf(
			"kcmdline - query the kernel command line\n"
			"\n"
			"usage: %s -g ARG...\n"
			"       %s -q ARG...\n"
			"\n"
			" -g     \033[3mprint the value for the requested argument\033[0m\n"
			" -q     \033[3mquery whether the requested argument is present (0 = yes)\033[0m\n"
			" -?     \033[3mshow this help text\033[0m\n"
			"\n", argv[0], argv[0]);
}

int main(int argc, char * argv[]) {
	/* Open */
	FILE * f = fopen("/proc/cmdline", "r");
	if (!f) return 1;

	/* Read */
	char * cmdline = malloc(4096); /* cmdline can't be longer than that */
	memset(cmdline, 0, 4096);
	size_t size = fread(cmdline, 1, 4096, f);
	if (cmdline[size-1] == '\n') cmdline[size-1] = '\0';
	fclose(f);

	/* Parse */
	args_map = hashmap_create(10);
	args_parse(cmdline);

	/* Figure out what we're doing */
	int opt;
	while ((opt = getopt(argc, argv, "?g:q:s")) != -1) {
		switch (opt) {
			case 'g':
				if (hashmap_has(args_map, optarg)) {
					char * tmp = (char*)hashmap_get(args_map, optarg);
					if (!tmp) {
						printf("%s\n", optarg); /* special case = present but not set should yield name of variable */
					} else {
						printf("%s\n", tmp);
					}
					return 0;
				} else {
					return 1;
				}
			case 'q':
				return !hashmap_has(args_map,optarg);
			case 's':
				return size;
			case '?':
				show_usage(argc, argv);
				return 1;
		}
	}

	fprintf(stdout, "%s\n", cmdline);
}
