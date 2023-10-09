/**
 * @brief Kernel commandline argument parser.
 *
 * Arguments to the kernel are provided from the bootloader and
 * provide information such as what mode to pass to init, or what
 * hard disk partition should be mounted as root. We parse them
 * into a hash table for easy lookup by key.
 *
 * An argument may be value-less (having no '='), in which case
 * its value in the hash table will be NULL but it will be present.
 * Examples of value-less arguments are @c lfbwc or @c noi965.
 *
 * Arguments with values can have quoted or unquoted values. Unquoted
 * values are terminated by a space or the end of the command line and
 * are not processed for escapes. Examples of arguments with
 * unquoted values are @c root=/dev/ram0 or @c start=live-session.
 *
 * Quoted values must started immediately with a double quote (").
 * Double quotes within the value may be escaped with a backslash (\).
 * Backslash can also be escaped. Any other character after a
 * backslash results in both a literal backslash and the following
 * character.
 *
 * If a quoted value is not properly terminated with an unescaped
 * double quote character, the entire argument will be ignored.
 *
 * @copyright This file is part of ToaruOS and is released under the terms
 *            of the NCSA / University of Illinois License - see LICENSE.md
 *            Copyright (C) 2011-2023 K. Lange
 */
#ifdef _KERNEL_
# include <kernel/string.h>
# include <kernel/args.h>
# include <kernel/tokenize.h>
# include <kernel/hashmap.h>
#endif

hashmap_t * kernel_args_map = NULL;

/**
 * @brief Determine if an argument was passed to the kernel.
 *
 * Check if an argument was provided to the kernel. If the argument is
 * a simple switch, a response of 1 can be considered "on" for that
 * argument; otherwise, this just notes that the argument was present,
 * so the caller should check whether it is correctly set.
 */
int args_present(const char * karg) {
	if (!kernel_args_map) return 0;
	return hashmap_has(kernel_args_map, karg);
}

/**
 * @brief Return the value associated with an argument provided to the kernel.
 */
char * args_value(const char * karg) {
	if (!kernel_args_map) return 0;
	return hashmap_get(kernel_args_map, karg);
}

/**
 * @brief Parse the given arguments to the kernel.
 *
 * @param arg A string containing all arguments, separated by spaces.
 */
void args_parse(const char * cmdline) {
	/* Sanity check... */
	if (!cmdline) { return; }
	if (!kernel_args_map) { kernel_args_map = hashmap_create(10); }
	char * argbuf = strdup(cmdline);
	char * x = argbuf;

	for (;;) {
		while (*x && *x == ' ') x++; /* skip spaces */
		if (!*x) break;

		char * value = NULL;
		char * key = x;
		while (*x && *x != '=' && *x != ' ') x++;

		if (*x == '=') {
			*x++ = '\0';
			if (*x == '"') {
				/* Start of quoted value */
				x++;
				value = x;
				char * w = x;

				while (*x && *x != '"') {
					if (*x == '\\') {
						x++;
						switch (*x) {
							case '"':  *w++ = '"';  x++; break;
							case '\\': *w++ = '\\'; x++; break;
							case '\0': goto _parse_error;
							default:   *w++ = '\\'; *w++ = *x++; break;
						}
					} else {
						*w++ = *x++;
					}
				}

				if (*x == '"') {
					*w = '\0';
					x++;
				} else if (*x == '\0') {
					goto _parse_error;
				}
			} else {
				/* Start of unquoted value */
				value = x;
				while (*x && *x != ' ') x++;
				if (*x == ' ') {
					*x = '\0';
					x++;
				}
			}
		} else if (*x == ' ') {
			/* Value-less argument where we need to set nil byte */
			*x++ = '\0';
		}

		hashmap_set(kernel_args_map, key, value ? strdup(value) : NULL);
	}

_parse_error:
	free(argbuf);
	return;
}

#ifndef _KERNEL_
char * args_from_procfs(void) {
	/* Open */
	FILE * f = fopen("/proc/cmdline", "r");
	if (!f) return NULL;

	/* Determine size */
	fseek(f, 0, SEEK_END);
	size_t size = ftell(f);
	fseek(f, 0, SEEK_SET);

	/* Read */
	char * cmdline = calloc(size + 1, 1);
	size_t rsize = fread(cmdline, 1, size, f);
	if (cmdline[rsize-1] == '\n') cmdline[rsize-1] = '\0';
	fclose(f);

	/* Parse */
	kernel_args_map = hashmap_create(10);
	args_parse(cmdline);

	return cmdline;
}

#endif

