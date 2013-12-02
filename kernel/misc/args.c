/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Kernel Argument Manager
 *
 * Arguments to the kernel are provided from the bootloader and
 * provide information such as what mode to pass to init, or what
 * hard disk partition should be mounted as root.
 *
 * This module provides access 
 */
#include <system.h>
#include <logging.h>
#include <args.h>
#include <tokenize.h>

char * cmdline = NULL;

list_t * kernel_args_list = NULL;

/**
 * Check if an argument was provided to the kernel. If the argument is
 * a simple switch, a response of 1 can be considered "on" for that
 * argument; otherwise, this just notes that the argument was present,
 * so the caller should check whether it is correctly set.
 */
int args_present(char * karg) {
	if (!kernel_args_list) return 0; /* derp */

	foreach(n, kernel_args_list) {
		struct kernel_arg * arg = (struct kernel_arg *)n->value;
		if (!strcmp(arg->name, karg)) {
			return 1;
		}
	}

	return 0;
}

/**
 * Return the value associated with an argument provided to the kernel.
 */
char * args_value(char * karg) {
	if (!kernel_args_list) return NULL; /* derp */

	foreach(n, kernel_args_list) {
		struct kernel_arg * arg = (struct kernel_arg *)n->value;
		if (!strcmp(arg->name, karg)) {
			return arg->value;
		}
	}

	return NULL;
}

/**
 * Parse the given arguments to the kernel.
 *
 * @param arg A string containing all arguments, separated by spaces.
 */
void args_parse(char * _arg) {
	/* Sanity check... */
	if (!_arg) { return; }

	char * arg = strdup(_arg);
	char * argv[1024];
	int argc = tokenize(arg, " ", argv);

	/* New let's parse the tokens into the arguments list so we can index by key */
	/* TODO I really need a dictionary/hashmap implementation */

	if (kernel_args_list) {
		/* Uh, crap. You've called me already... */
		list_destroy(kernel_args_list);
		list_free(kernel_args_list);
	}
	kernel_args_list = list_create();

	for (int i = 0; i < argc; ++i) {
		char * c = strdup(argv[i]);

		struct kernel_arg * karg = malloc(sizeof(struct kernel_arg));
		karg->name = c;
		karg->value = NULL;
		/* Find the first = and replace it with a null */
		char * v = c;
		while (*v) {
			if (*v == '=') {
				*v = '\0';
				v++;
				karg->value = v;
				goto _break;
			}
			v++;
		}

_break:
		list_insert(kernel_args_list, karg);
	}

	free(arg);

}

