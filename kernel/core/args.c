/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Kernel Argument Parser
 *
 * Parses arguments passed by, ie, a Multiboot bootloader.
 *
 * Part of the ToAruOS Kernel.
 * (C) 2011 Kevin Lange
 */
#include <system.h>

extern void bochs_install_wallpaper(char *);

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
		char * pch_i;
		char * save_i;
		char * argp[1024];
		int    argc = 0;
		pch_i = strtok_r(argv[i],"=",&save_i);
		if (!pch_i) { continue; }
		while (pch_i != NULL) {
			argp[argc] = (char *)pch_i;
			++argc;
			pch_i = strtok_r(NULL,"=",&save_i);
		}
		argp[argc] = NULL;

		if (!strcmp(argp[0],"vid")) {
			if (argc < 2) { kprintf("vid=?\n"); continue; }
			if (!strcmp(argp[1],"qemu")) {
				/* Bochs / Qemu Video Device */
				graphics_install_bochs();
				ansi_init(&bochs_write, 128, 64);
			} else {
				kprintf("Unrecognized video adapter: %s\n", argp[1]);
			}
		} else if (!strcmp(argp[0],"wallpaper")) {
			if (argc < 2) { kprintf("wallpaper=?\n"); continue; }
			bochs_install_wallpaper(argp[1]);
		}
	}
}

