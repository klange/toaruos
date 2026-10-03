/**
 * @file  kernel/generic.c
 * @brief Architecture-neutral startup sequences.
 *
 * The generic startup sequence is broken into two parts:
 * @c generic_startup should be called as soon as the platform
 * has configured memory and is ready for the VFS and scheduler
 * to be initialized. @c generic_main should be called after
 * the platform has set up its own device drivers, loaded any
 * early filesystems, and is ready to yield control to init.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
 */
#include <kernel/generic.h>
#include <kernel/args.h>
#include <kernel/process.h>
#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/misc.h>

extern void tarfs_register_init(void);
extern void tmpfs_register_init(void);
extern void tasking_start(void);
extern void zero_initialize(void);
extern void procfs_initialize(void);
extern void random_initialize(void);
extern void snd_install(void);
extern void net_install(void);
extern void console_initialize(void);
extern void modules_install(void);

void generic_startup(void) {
	args_parse(arch_get_cmdline());
	initialize_process_tree();
	vfs_install();
	tarfs_register_init();
	tmpfs_register_init();
	map_vfs_directory("/dev");
	console_initialize();
	zero_initialize();
	procfs_initialize();
	random_initialize();
	snd_install();
	net_install();
	tasking_start();
	modules_install();
}

int generic_main(void) {
	if (args_present("root")) {
		const char * root_type = "tar";
		if (args_present("root_type")) {
			root_type = args_value("root_type");
		}
		if (args_present("migrate")) {
			if (strcmp(root_type, "tar")) {
				arch_fatal_prepare();
				dprintf("migrate: can not migrate from non-tar root\n");
				arch_fatal();
			}
			vfs_mount_type("tmpfs","x,755","/");
			extern int tarfs_unpack(char * from_file);
			tarfs_unpack(args_value("root"));
		} else {
			vfs_mount_type(root_type,args_value("root"),"/");
		}
	}

	const char * boot_arg = NULL;

	if (args_present("args")) {
		boot_arg = strdup(args_value("args"));
	}

	const char * boot_app = "/bin/init";
	if (args_present("init")) {
		boot_app = args_value("init");
	}

	dprintf("generic: Running %s as init process.\n", boot_app);

	process_chdir((process_t*)this_core->current_process, fs_fresh_descriptor(fs_root, 0, fs_alloc_path_from("/", "system")));

	int argc = boot_arg ? 2 : 1;
	char ** args = calloc(argc + 1, sizeof(char *));
	args[0] = strdup(boot_app);
	args[1] = boot_arg ? strdup(boot_arg) : NULL;

	int error = exec(boot_app, argc, args, calloc(1, sizeof(char*)), 0);

	dprintf("generic: Failed to execute %s (%d).\n", boot_app, error);
	switch_task(0);
	return 0;
}
