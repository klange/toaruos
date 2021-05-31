/**
 * @brief Kernel commandline argument parser.
 *
 * Arguments to the kernel are provided from the bootloader and
 * provide information such as what mode to pass to init, or what
 * hard disk partition should be mounted as root. We parse them
 * into a hash table for easy lookup by key.
 *
 * @copyright This file is part of ToaruOS and is released under the terms
 *            of the NCSA / University of Illinois License - see LICENSE.md
 *            Copyright (C) 2011-2021 K. Lange
 */
#include <kernel/string.h>
#include <kernel/args.h>
#include <kernel/tokenize.h>
#include <kernel/hashmap.h>

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
void args_parse(const char * _arg) {
	/* Sanity check... */
	if (!_arg) { return; }

	char * arg = strdup(_arg);
	char * argv[1024];
	int argc = tokenize(arg, " ", argv);

	/* New let's parse the tokens into the arguments list so we can index by key */
	if (!kernel_args_map) {
		kernel_args_map = hashmap_create(10);
	}

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
		hashmap_set(kernel_args_map, name, value);
	}

	free(arg);
}

