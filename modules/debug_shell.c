/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014 Kevin Lange
  *
 * Kernel Debug Shell
 */
#include <system.h>
#include <fs.h>
#include <printf.h>
#include <logging.h>
#include <process.h>
#include <version.h>
#include <termios.h>
#include <tokenize.h>
#include <hashmap.h>
#include <pci.h>
#include <pipe.h>
#include <elf.h>
#include <module.h>

#include <mod/shell.h>

/*
 * This is basically the same as a userspace buffered/unbuffered
 * termio call. These are the same sorts of things I would use in
 * a text editor in userspace, but with the internal kernel calls
 * rather than system calls.
 */
static struct termios old;

void tty_set_unbuffered(fs_node_t * dev) {
	ioctl_fs(dev, TCGETS, &old);
	struct termios new = old;
	new.c_lflag &= (~ICANON & ~ECHO);
	ioctl_fs(dev, TCSETSF, &new);
}

void tty_set_buffered(fs_node_t * dev) {
	ioctl_fs(dev, TCSETSF, &old);
}

void tty_set_vintr(fs_node_t * dev, char vintr) {
	struct termios tmp;
	ioctl_fs(dev, TCGETS, &tmp);
	tmp.c_cc[VINTR] = vintr;
	ioctl_fs(dev, TCSETSF, &tmp);
}

/*
 * Quick readline implementation.
 *
 * Most of these TODOs are things I've done already in older code:
 * TODO tabcompletion would be nice
 * TODO history is also nice
 */
int debug_shell_readline(fs_node_t * dev, char * linebuf, int max) {
	int read = 0;
	tty_set_unbuffered(dev);
	while (read < max) {
		uint8_t buf[1];
		int r = read_fs(dev, 0, 1, (unsigned char *)buf);
		if (!r) {
			debug_print(WARNING, "Read nothing?");
			continue;
		}
		linebuf[read] = buf[0];
		if (buf[0] == '\n') {
			fprintf(dev, "\n");
			linebuf[read] = 0;
			break;
		} else if (buf[0] == 0x08) {
			if (read > 0) {
				fprintf(dev, "\010 \010");
				read--;
				linebuf[read] = 0;
			}
		} else if (buf[0] < ' ') {
			switch (buf[0]) {
				case 0x04:
					if (read == 0) {
						fprintf(dev, "exit\n");
						sprintf(linebuf, "exit");
						return strlen(linebuf);
					}
					break;
				case 0x0C: /* ^L */
					/* Should reset display here */
					break;
				default:
					/* do nothing */
					break;
			}
		} else {
			fprintf(dev, "%c", buf[0]);
			read += r;
		}
	}
	tty_set_buffered(dev);
	return read;
}

/*
 * Tasklet for running a userspace application.
 */
static void debug_shell_run_sh(void * data, char * name) {

	char * argv[] = {
		data,
		NULL
	};
	int argc = 0;
	while (argv[argc]) {
		argc++;
	}
	system(argv[0], argc, argv); /* Run shell */

	task_exit(42);
}

static hashmap_t * shell_commands_map = NULL;

/*
 * Shell commands
 */
static int shell_create_userspace_shell(fs_node_t * tty, int argc, char * argv[]) {
	int pid = create_kernel_tasklet(debug_shell_run_sh, "[[k-sh]]", "/bin/sh");
	fprintf(tty, "Shell started with pid = %d\n", pid);
	int status;
	waitpid(pid,&status,0);
	return status;
}

static int shell_replace_login(fs_node_t * tty, int argc, char * argv[]) {
	/* We need to fork to get a clean task space */
	create_kernel_tasklet(debug_shell_run_sh, "[[k-sh]]", "/bin/login");
	/* Then exit the shell process */
	task_exit(0);
	/* unreachable */
	return 0;
}

static int shell_echo(fs_node_t * tty, int argc, char * argv[]) {
	for (int i = 1; i < argc; ++i) {
		fprintf(tty, "%s ", argv[i]);
	}
	fprintf(tty, "\n");
	return 0;
}

static int dumb_strcmp(void * a, void *b) {
	return strcmp(a, b);
}

static void dumb_sort(void ** list, size_t length, int (*compare)(void*,void*)) {
	for (unsigned int i = 0; i < length-1; ++i) {
		for (unsigned int j = 0; j < length-1; ++j) {
			if (compare(list[j], list[j+1]) > 0) {
				void * t = list[j+1];
				list[j+1] = list[j];
				list[j] = t;
			}
		}
	}
}

static void print_spaces(fs_node_t * tty, int num_spaces) {
	for (int i = 0; i < num_spaces; ++i) {
		fprintf(tty, " ");
	}
}

static int shell_help(fs_node_t * tty, int argc, char * argv[]) {
	list_t * hash_keys = hashmap_keys(shell_commands_map);

	char ** keys = malloc(sizeof(char *) * hash_keys->length);

	unsigned int i = 0;
	unsigned int max_width = 0;

	foreach(_key, hash_keys) {
		char * key = (char *)_key->value;
		keys[i] = key;
		i++;
		if (strlen(key) > max_width) {
			max_width = strlen(key);
		}
	}

	dumb_sort((void **)keys, hash_keys->length, &dumb_strcmp);

	for (i = 0; i < hash_keys->length; ++i) {
		struct shell_command * c = hashmap_get(shell_commands_map, keys[i]);
		fprintf(tty, "\033[1;32m%s\033[0m ", c->name);
		print_spaces(tty, max_width- strlen(c->name));
		fprintf(tty, "- %s\n", c->description);
	}

	free(keys);
	list_free(hash_keys);
	free(hash_keys);

	return 0;
}

static int shell_cd(fs_node_t * tty, int argc, char * argv[]) {
	if (argc < 2) {
		return 1;
	}
	char * newdir = argv[1];
	char * path = canonicalize_path(current_process->wd_name, newdir);
	fs_node_t * chd = kopen(path, 0);
	if (chd) {
		if ((chd->flags & FS_DIRECTORY) == 0) {
			return 1;
		}
		close_fs(chd);
		free(current_process->wd_name);
		current_process->wd_name = malloc(strlen(path) + 1);
		memcpy(current_process->wd_name, path, strlen(path) + 1);
		return 0;
	} else {
		return 1;
	}
}

static int shell_ls(fs_node_t * tty, int argc, char * argv[]) {
	/* Okay, we're going to take the working directory... */
	fs_node_t * wd = kopen(current_process->wd_name, 0);
	uint32_t index = 0;
	struct dirent * kentry = readdir_fs(wd, index);
	while (kentry) {
		fprintf(tty, "%s\n", kentry->name);
		free(kentry);

		index++;
		kentry = readdir_fs(wd, index);
	}
	close_fs(wd);
	return 0;
}

static int shell_log(fs_node_t * tty, int argc, char * argv[]) {
	if (argc < 2) {
		fprintf(tty, "Log level is currently %d.\n", debug_level);
		fprintf(tty, "Serial logging is %s.\n", !!debug_file ? "enabled" : "disabled");
		fprintf(tty, "Usage: log [on|off] [<level>]\n");
	} else {
		if (!strcmp(argv[1], "direct")) {
			debug_file = kopen("/dev/ttyS0", 0);
			if (argc > 2) {
				debug_level = atoi(argv[2]);
			}
		} else if (!strcmp(argv[1], "on")) {
			debug_file = tty;
			if (argc > 2) {
				debug_level = atoi(argv[2]);
			}
		} else if (!strcmp(argv[1], "off")) {
			debug_file = NULL;
		}
	}
	return 0;
}

static void scan_hit_list(uint32_t device, uint16_t vendorid, uint16_t deviceid, void * extra) {

	fs_node_t * tty = extra;

	fprintf(tty, "%2x:%2x.%d (%4x, %4x:%4x) %s %s\n",
			(int)pci_extract_bus(device),
			(int)pci_extract_slot(device),
			(int)pci_extract_func(device),
			(int)pci_find_type(device),
			vendorid,
			deviceid,
			pci_vendor_lookup(vendorid),
			pci_device_lookup(vendorid,deviceid));

	fprintf(tty, " BAR0: 0x%8x\n", pci_read_field(device, PCI_BAR0, 4));
	fprintf(tty, " BAR1: 0x%8x\n", pci_read_field(device, PCI_BAR1, 4));
	fprintf(tty, " BAR2: 0x%8x\n", pci_read_field(device, PCI_BAR2, 4));
	fprintf(tty, " BAR3: 0x%8x\n", pci_read_field(device, PCI_BAR3, 4));
	fprintf(tty, " BAR4: 0x%8x\n", pci_read_field(device, PCI_BAR4, 4));
	fprintf(tty, " BAR6: 0x%8x\n", pci_read_field(device, PCI_BAR5, 4));

}

static int shell_pci(fs_node_t * tty, int argc, char * argv[]) {
	pci_scan(&scan_hit_list, -1, tty);
	return 0;
}

static int shell_uid(fs_node_t * tty, int argc, char * argv[]) {
	if (argc < 2) {
		fprintf(tty, "uid=%d\n", current_process->user);
	} else {
		current_process->user = atoi(argv[1]);
	}
	return 0;
}

char * special_thing = "I am a string from the kernel.\n";

static int shell_mod(fs_node_t * tty, int argc, char * argv[]) {
	if (argc < 2) {
		fprintf(tty, "%s: expected argument\n", argv[0]);
		return 1;
	}
	fs_node_t * file = kopen(argv[1], 0);
	if (!file) {
		fprintf(tty, "%s: Error loading module '%s': File not found\n", argv[0], argv[1]);
		return 1;
	}
	close_fs(file);

	module_data_t * mod_info = module_load(argv[1]);
	if (!mod_info) {
		fprintf(tty, "%s: Error loading module '%s'\n", argv[0], argv[1]);
		return 1;
	}

	fprintf(tty, "Module '%s' loaded at 0x%x\n", mod_info->mod_info->name, mod_info->bin_data);

	return 0;
}

static int shell_symbols(fs_node_t * tty, int argc, char * argv[]) {
	extern char kernel_symbols_start[];
	extern char kernel_symbols_end[];

	struct ksym {
		uintptr_t addr;
		char name[];
	} * k = (void*)&kernel_symbols_start;

	while ((uintptr_t)k < (uintptr_t)&kernel_symbols_end) {
		fprintf(tty, "0x%x - %s\n", k->addr, k->name);
		k = (void *)((uintptr_t)k + sizeof(uintptr_t) + strlen(k->name) + 1);
	}

	return 0;
}

static int shell_print(fs_node_t * tty, int argc, char * argv[]) {

	if (argc < 3) {
		fprintf(tty, "print format_string symbol_name\n");
		return 1;
	}

	char * format = argv[1];
	char * symbol = argv[2];
	int deref = 0;

	if (symbol[0] == '*') {
		symbol = &symbol[1];
		deref = 1;
	}

	extern char kernel_symbols_start[];
	extern char kernel_symbols_end[];

	struct ksym {
		uintptr_t addr;
		char name[];
	} * k = (void*)&kernel_symbols_start;

	while ((uintptr_t)k < (uintptr_t)&kernel_symbols_end) {
		if (!strcmp(symbol, k->name)) {
			if (deref) {
				fprintf(tty, format, k->addr);
			} else {
				fprintf(tty, format, *((uintptr_t *)k->addr));
			}
			fprintf(tty, "\n");
			break;
		}
		k = (void *)((uintptr_t)k + sizeof(uintptr_t) + strlen(k->name) + 1);
	}

	return 0;
}

static int shell_modules(fs_node_t * tty, int argc, char * argv[]) {
	list_t * hash_keys = hashmap_keys(modules_get_list());
	foreach(_key, hash_keys) {
		char * key = (char *)_key->value;
		module_data_t * mod_info = hashmap_get(modules_get_list(), key);

		fprintf(tty, "0x%x {.init=0x%x, .fini=0x%x} %s",
				mod_info->bin_data,
				mod_info->mod_info->initialize,
				mod_info->mod_info->finalize,
				mod_info->mod_info->name);

		if (mod_info->deps) {
			unsigned int i = 0;
			fprintf(tty, " Deps: ");
			while (i < mod_info->deps_length) {
				fprintf(tty, "%s ", &mod_info->deps[i]);
				i += strlen(&mod_info->deps[i]) + 1;
			}
		}

		fprintf(tty, "\n");
	}

	return 0;
}

static int shell_rdtsc(fs_node_t * tty, int argc, char * argv[]) {
	uint64_t x;
	asm volatile (".byte 0x0f, 0x31" : "=A" (x));

	fprintf(tty, "0x%x%x\n", (uint32_t)(x >> 32), (uint32_t)(x & 0xFFFFFFFF));

	return 0;
}

static int shell_mhz(fs_node_t * tty, int argc, char * argv[]) {

	uint64_t x, y;

	asm volatile (".byte 0x0f, 0x31" : "=A" (x));

	unsigned long s, ss;
	relative_time(1, 0, &s, &ss);
	sleep_until((process_t *)current_process, s, ss);
	switch_task(0);

	asm volatile (".byte 0x0f, 0x31" : "=A" (y));

	uint64_t diff = y - x;
	uint32_t f = diff >> 15;
	uint32_t mhz = f / 30;

	fprintf(tty, "%d MHz\n", mhz);

	return 0;
}

/*
 * Determine the size of a smart terminal that we don't have direct
 * termios access to. This is done by sending a cursor-move command
 * that will put the cursor into the lower right corner and then
 * requesting the cursor position report. We then read and parse
 * the position report. In the case where the terminal on the other
 * end is actually dumb, we end up waiting for some input and
 * then timing out.
 * TODO with asyncio support, the timeout should actually work.
 *      consider also using an alarm (which I also don't have)
 */
static void divine_size(fs_node_t * dev, int * width, int * height) {
	char tmp[100];
	int read = 0;
	unsigned long start_tick = timer_ticks;
	memset(tmp, 0, sizeof(tmp));
	/* Move cursor, Request position, Reset cursor */
	tty_set_unbuffered(dev);
	fprintf(dev, "\033[1000;1000H\033[6n\033[H");
	while (1) {
		char buf[1];
		int r = read_fs(dev, 0, 1, (unsigned char *)buf);
		if (r > 0) {
			if (buf[0] != 'R') {
				if (read > 1) {
					tmp[read-2] = buf[0];
				}
				read++;
			} else {
				break;
			}
		}
		if (timer_ticks - start_tick >= 2) {
			/*
			 * We've timed out. This will only be triggered
			 * when we eventually receive something, though
			 */
			*width  = 80;
			*height = 23;
			/* Clear and return */
			fprintf(dev, "\033[J");
			tty_set_buffered(dev);
			return;
		}
	}
	/* Clear */
	fprintf(dev, "\033[J");
	/* Break up the result into two strings */

	for (unsigned int i = 0; i < strlen(tmp); i++) {
		if (tmp[i] == ';') {
			tmp[i] = '\0';
			break;
		}
	}
	char * h = (char *)((uintptr_t)tmp + strlen(tmp)+1);
	/* And then parse it into numbers */
	*height = atoi(tmp);
	*width  = atoi(h);
	tty_set_buffered(dev);
}

static int shell_divinesize(fs_node_t * tty, int argc, char * argv[]) {
	struct winsize size = {0,0,0,0};

	/* Attempt to divine the terminal size. Changing the window size after this will do bad things */
	int width, height;
	divine_size(tty, &width, &height);

	fprintf(tty, "Identified size: %d x %d\n", width, height);

	size.ws_row = height;
	size.ws_col = width;

	ioctl_fs(tty, TIOCSWINSZ, &size);

	return 0;
}

static int shell_fix_mouse(fs_node_t * tty, int argc, char * argv[]) {

	fs_node_t * mouse = kopen("/dev/mouse", 0);
	if (mouse) {
		ioctl_fs(mouse, 1, NULL);
		close_fs(mouse);
	}

	return 0;
}

static int shell_mount(fs_node_t * tty, int argc, char * argv[]) {
	if (argc < 4) {
		fprintf(tty, "Usage: %s type device mountpoint\n", argv[0]);
		return 1;
	}

	return -vfs_mount_type(argv[1], argv[2], argv[3]);
}

static int shell_exit(fs_node_t * tty, int argc, char * argv[]) {
	kexit(0);
	return 0;
}

static struct shell_command shell_commands[] = {
	{"shell", &shell_create_userspace_shell,
		"Runs a userspace shell on this tty."},
	{"login", &shell_replace_login,
		"Replace the debug shell with /bin/login."},
	{"echo",  &shell_echo,
		"Prints arguments."},
	{"help",  &shell_help,
		"Prints a list of possible shell commands and their descriptions."},
	{"cd",    &shell_cd,
		"Change current directory."},
	{"ls",    &shell_ls,
		"List files in current or other directory."},
	{"log", &shell_log,
		"Configure serial debug logging."},
	{"pci", &shell_pci,
		"Print PCI devices, as well as their names and BARs."},
	{"uid", &shell_uid,
		"Change the effective user id of the shell."},
	{"mod", &shell_mod,
		"[testing] Module loading."},
	{"symbols", &shell_symbols,
		"Dump symbol table."},
	{"print", &shell_print,
		"[dangerous] Print the value of a symbol using a format string."},
	{"modules", &shell_modules,
		"Print names and addresses of all loaded modules."},
	{"divine-size", &shell_divinesize,
		"Attempt to discover TTY size of serial."},
	{"fix-mouse", &shell_fix_mouse,
		"Attempt to reset mouse device."},
	{"mount", &shell_mount,
		"Mount a filesystemp."},
	{"rdtsc", &shell_rdtsc,
		"Read the TSC, if available."},
	{"mhz", &shell_mhz,
		"Use TSC to determine clock speed."},
	{"exit", &shell_exit,
		"Quit the shell."},
	{NULL, NULL, NULL}
};

void debug_shell_install(struct shell_command * sh) {
	hashmap_set(shell_commands_map, sh->name, sh);
}

/*
 * A TTY object to pass to the tasklets for handling
 * serial-tty interaction. This probably shouldn't
 * be done as tasklets - TTYs should just be able
 * to wrap existing fs_nodes themselves, but that's
 * a problem for another day.
 */
struct tty_o {
	fs_node_t * node;
	fs_node_t * tty;
};

/*
 * These tasklets handle tty-serial interaction.
 */
static void debug_shell_handle_in(void * data, char * name) {
	struct tty_o * tty = (struct tty_o *)data;

	while (1) {
		uint8_t buf[1];
		int r = read_fs(tty->tty, 0, 1, (unsigned char *)buf);
		write_fs(tty->node, 0, r, buf);
	}
}

static void debug_shell_handle_out(void * data, char * name) {
	struct tty_o * tty = (struct tty_o *)data;

	while (1) {
		uint8_t buf[1];
		int r = read_fs(tty->node, 0, 1, (unsigned char *)buf);
		write_fs(tty->tty, 0, r, buf);
	}
}

static void debug_shell_actual(void * data, char * name) {

	current_process->image.entry = 0;
	fs_node_t * tty = (fs_node_t *)data;

	/* Our prompt will include the version number of the current kernel */
	char version_number[1024];
	sprintf(version_number, __kernel_version_format,
			__kernel_version_major,
			__kernel_version_minor,
			__kernel_version_lower,
			__kernel_version_suffix);

	/* Initialize the shell commands map */
	int retval = 0;

	while (1) {
		char command[512];

		/* Print out the prompt */
		if (retval) {
			fprintf(tty, "\033[1;34m%s-%s \033[1;31m%d\033[1;34m %s#\033[0m ", __kernel_name, version_number, retval, current_process->wd_name);
		} else {
			fprintf(tty, "\033[1;34m%s-%s %s#\033[0m ", __kernel_name, version_number, current_process->wd_name);
		}

		/* Read a line */
		debug_shell_readline(tty, command, 511);

		char * arg = strdup(command);
		char * argv[1024];  /* Command tokens (space-separated elements) */
		int argc = tokenize(arg, " ", argv);

		if (!argc) continue;

		/* Parse the command string */
		struct shell_command * sh = hashmap_get(shell_commands_map, argv[0]);
		if (sh) {
			retval = sh->function(tty, argc, argv);
		} else {
			fprintf(tty, "Unrecognized command: %s\n", argv[0]);
		}

		free(arg);
	}

}

/*
 * Tasklet for managing the kernel serial console.
 * This is basically a very simple shell, with access
 * to some internal kernel commands, and (eventually)
 * debugging routines.
 */
static void debug_shell_run(void * data, char * name) {
	/*
	 * We will run on the first serial port.
	 * TODO detect that this failed
	 */
	fs_node_t * tty = kopen("/dev/ttyS0", 0);

	fs_node_t * fs_master;
	fs_node_t * fs_slave;

	pty_create(NULL, &fs_master, &fs_slave);

	/* Attach the serial to the TTY interface */
	struct tty_o _tty = {.node = fs_master, .tty = tty};

	create_kernel_tasklet(debug_shell_handle_in,  "[kttydebug-in]",  (void *)&_tty);
	create_kernel_tasklet(debug_shell_handle_out, "[kttydebug-out]", (void *)&_tty);

	/* Set the device to be the actual TTY slave */
	tty = fs_slave;

	fs_master->refcount = -1;
	fs_slave->refcount = -1;

	current_process->fds->entries[0] = tty;
	current_process->fds->entries[1] = tty;
	current_process->fds->entries[2] = tty;
	current_process->fds->length = 3;

	tty_set_vintr(tty, 0x02);

	fprintf(tty, "\n\n"
			"Serial debug console started.\n"
			"Type `help` for a list of commands.\n"
			"To access a userspace shell, type `shell`.\n"
			"Use ^B to send SIGINT instead of ^C.\n"
			"\n");

	debug_shell_actual(tty, name);
}

int debug_shell_start(void) {
	/* Setup shell commands */
	shell_commands_map = hashmap_create(10);
	struct shell_command * sh = &shell_commands[0];
	while (sh->name) {
		hashmap_set(shell_commands_map, sh->name, sh);
		sh++;
	}

	debug_hook = debug_shell_actual;

	int i = create_kernel_tasklet(debug_shell_run, "[kttydebug]", NULL);
	debug_print(NOTICE, "Started tasklet with pid=%d", i);

	return 0;
}

int debug_shell_stop(void) {
	debug_print(NOTICE, "Tried to unload debug shell, but debug shell has no real shutdown routine. Don't do that!");
	return 0;
}

MODULE_DEF(debugshell, debug_shell_start, debug_shell_stop);
MODULE_DEPENDS(serial);
