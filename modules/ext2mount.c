/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014 Kevin Lange
 */
#include <system.h>
#include <module.h>
#include <fs.h>
#include <printf.h>
#include <mod/shell.h>

extern fs_node_t * ext2_fs_mount(char * device, char * mount_path);

DEFINE_SHELL_FUNCTION(mount, "Mount an ext2 filesystem") {

	if (argc < 3) {
		fprintf(tty, "Usage: %s device mount_path", argv[0]);
		return 1;
	}

	ext2_fs_mount(argv[1], argv[2]);

	return 0;
}

static int init(void) {
	BIND_SHELL_FUNCTION(mount);
	return 0;
}

static int fini(void) {
	return 0;
}

MODULE_DEF(ext2mount, init, fini);
MODULE_DEPENDS(debugshell);
MODULE_DEPENDS(ext2);
