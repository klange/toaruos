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
#include <logging.h>
#include <ata.h>

/* XXX: This should be moved */
void ext2_disk_mount(uint32_t offset_sector, uint32_t max_sector);
int read_partition_map(int device);

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
			pch_i = strtok_r(NULL,"=,",&save_i);
		}
		argp[argc] = NULL;

		if (!strcmp(argp[0],"vid")) {
			if (argc < 2) { kprintf("vid=?\n"); continue; }
			uint16_t x, y;
			if (argc < 4) {
				x = 1024;
				y = 768;
			} else {
				x = atoi(argp[2]);
				y = atoi(argp[3]);
				debug_print(NOTICE, "Requested display resolution is %dx%d", x, y);
			}
			if (!strcmp(argp[1],"qemu")) {
				/* Bochs / Qemu Video Device */
				graphics_install_bochs(x,y);
			} else if (!strcmp(argp[1],"preset")) {
				graphics_install_preset(x,y);
			} else {
				debug_print(WARNING, "Unrecognized video adapter: %s", argp[1]);
			}
		} else if (!strcmp(argp[0],"hdd")) {
			if (argc > 1) {
				debug_print(INFO, "Scanning disk...");
				if (read_partition_map(0)) {
					debug_print(ERROR, "Failed to read MBR.");
					continue;
				}
				int partition = atoi(argp[1]);
				debug_print(NOTICE, "Selected partition %d starts at sector %d", partition, mbr.partitions[partition].lba_first_sector);
				ext2_disk_mount(mbr.partitions[partition].lba_first_sector,mbr.partitions[partition].sector_count);
			} else {
				ext2_disk_mount(0, 0);
			}
		} else if (!strcmp(argp[0],"single")) {
			boot_arg = "--single";
		} else if (!strcmp(argp[0],"lite")) {
			boot_arg = "--special";
		} else if (!strcmp(argp[0],"vgaterm")) {
			boot_arg = "--vga";
		} else if (!strcmp(argp[0],"start")) {
			if (argc < 2) {
				debug_print(WARNING, "Expected an argument to kernel option `start`. Ignoring.");
				continue;
			}
			boot_arg_extra = argp[1];
		} else if (!strcmp(argp[0],"logtoserial")) {
			if (argc > 1) {
				debug_level = atoi(argp[1]);
			} else {
				debug_level = NOTICE; /* INFO is a bit verbose for a default */
			}
			kprint_to_serial = 1;
			debug_print(NOTICE, "Kernel serial logging enabled at level %d.", debug_level);
		} else if (!strcmp(argp[0],"kernel-term")) {
			if (argc > 1) {
				debug_level = atoi(argp[1]);
			} else {
				debug_level = NOTICE; /* INFO is a bit verbose for a default */
			}
			kprint_to_screen = 1;
			debug_print(NOTICE, "Kernel serial logging enabled at level %d.", debug_level);
		} else if (!strcmp(argp[0],"read-mbr")) {
			read_partition_map(0);
		}
	}
}

