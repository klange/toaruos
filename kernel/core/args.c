/*
 * Kernel Argument Parser
 *
 * Parses arguments passed by, ie, a Multiboot bootloader.
 *
 * Part of the ToAruOS Kernel.
 * (C) 2011 Kevin Lange
 */
#include <system.h>

/**
 * Parse the given arguments to the kernel.
 *
 * @param arg A string containing all arguments, separated by spaces.
 */
void
parse_args(
		char * arg /* Arguments */
		) {
	/* Sanity check... */
	if (!arg) { return; }
	char * pch;         /* Tokenizer pointer */
	char * save;        /* We use the reentrant form of strtok */
	char * argv[1024];  /* Command tokens (space-separated elements) */
	int    tokenid = 0; /* argc, basically */

	/* Tokenize the arguments, splitting at spaces */
	pch = strtok_r(arg," ",&save);
	if (!pch) { return; }
	while (pch != NULL) {
		argv[tokenid] = (char *)pch;
		++tokenid;
		pch = strtok_r(NULL," ",&save);
	}
	argv[tokenid] = NULL;
	/* Tokens are now stored in argv. */


	for (int i = 0; i < tokenid; ++i) {
		/* Parse each provided argument */

		if (!strcmp(argv[i],"vid=qemu")) {
			/* Bochs / Qemu Video Device */
			graphics_install_bochs();
			ansi_init(&bochs_write, 128, 64);
		}
	}
}

/*
 * vim:tabstop=4
 * vim:noexpandtab
 */
