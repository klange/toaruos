/*
 * vim:tabstop=4
 * vim:noexpandtab
 *
 * ToAruOS Kernel Debugger Shell
 *
 * Part of the ToAruOS Kernel, under the NCSA license
 *
 * Copyright 2011 Kevin Lange
 *
 *   (Preliminary documentation based on intended future use; currently,
 *    this is just a file system explorer)
 *
 * This is a kernel debugging shell that allows basic, sh-like operation
 * of the system while it is in use, without other tasks running in the
 * background. While the debug shell is running, the tasker is disabled
 * and the kernel will remainin on its current task, allowing users to
 * display registry and memory information relavent to the current task.
 *
 */
#include <system.h>
#include <fs.h>
#include <multiboot.h>
#include <ata.h>
#include <ext2.h>
#include <list.h>
#include <tree.h>
#include <process.h>


struct {
	char path[1024];
	char * username;
	char * hostname;
	uint16_t month, day, hours, minutes, seconds;
	fs_node_t * node;
} shell;

#define SHELL_COMMANDS 512
typedef uint32_t(*shell_command_t) (int argc, char ** argv);
char * shell_commands[SHELL_COMMANDS];
shell_command_t shell_pointers[SHELL_COMMANDS];
uint32_t shell_commands_len = 0;

#define SHELL_HISTORY_ENTRIES 128
char * shell_history[SHELL_HISTORY_ENTRIES];
size_t shell_history_count  = 0;
size_t shell_history_offset = 0;

size_t shell_scroll = 0;
char   shell_temp[1024];

void
shell_history_insert(char * str) {
	if (shell_history_count == SHELL_HISTORY_ENTRIES) {
		free(shell_history[shell_history_offset]);
		shell_history[shell_history_offset] = str;
		shell_history_offset = (shell_history_offset + 1) % SHELL_HISTORY_ENTRIES;
	} else {
		shell_history[shell_history_count] = str;
		shell_history_count++;
	}
}

char *
shell_history_get(size_t item) {
	return shell_history[(item + shell_history_offset) % SHELL_HISTORY_ENTRIES];
}

char *
shell_history_prev(size_t item) {
	return shell_history_get(shell_history_count - item);
}

void
redraw_shell() {
	kprintf("\033[1m[\033[1;33m%s \033[1;32m%s \033[1;31m%d/%d \033[1;34m%d:%d:%d\033[0m \033[0m%s\033[1m]\033[0m\n\033[1;32m$\033[0m ",
			shell.username, shell.hostname, shell.month, shell.day, shell.hours, shell.minutes, shell.seconds, shell.path);
}

void
init_shell() {
	shell.node = fs_root;
	shell.username = "kernel";
	shell.hostname = "toaru";
	shell.path[0]  = '/';
	shell.path[1]  = '\0';
}

void
shell_install_command(char * name, shell_command_t func) {
	if (shell_commands_len == SHELL_COMMANDS) {
		kprintf("Ran out of space for static shell commands. The maximum number of commands is %d\n", SHELL_COMMANDS);
		return;
	}
	shell_commands[shell_commands_len] = name;
	shell_pointers[shell_commands_len] = func;
	shell_commands_len++;
}

shell_command_t shell_find(char * str) {
	for (uint32_t i = 0; i < shell_commands_len; ++i) {
		if (!strcmp(str, shell_commands[i])) {
			return shell_pointers[i];
		}
	}
	return NULL;
}

void shell_update_time() {
	get_date(&shell.month, &shell.day);
	get_time(&shell.hours, &shell.minutes, &shell.seconds);
}

void shell_exec(char * buffer, int size) {
	/*
	 * Tokenize the command
	 */
	char * pch;
	char * cmd;
	char * save;
	/* First off, let's check if it's a history request */
	if (buffer[0] == '!') {
		uint32_t x = atoi((char *)((uint32_t)buffer + 1));
		if (x <= shell_history_count) {
			buffer = shell_history_get(x - 1);
			size = strlen(buffer);
		} else {
			kprintf("history: invalid index %d\n", x);
			return;
		}
	}
	char * history = malloc(sizeof(char) * (size + 1));
	memcpy(history, buffer, strlen(buffer) + 1);
	pch = strtok_r(buffer," ",&save);
	cmd = pch;
	if (!cmd) {
		free(history);
		return;
	}
	shell_history_insert(history);
	char * argv[1024]; /* Command tokens (space-separated elements) */
	int tokenid = 0;
	while (pch != NULL) {
		argv[tokenid] = (char *)pch;
		++tokenid;
		pch = strtok_r(NULL," ",&save);
	}
	argv[tokenid] = NULL;
	shell_command_t func = shell_find(argv[0]);
	if (func) {
		func(tokenid, argv);
	} else {
		/* Alright, here we go */
		char * filename = malloc(sizeof(char) * 1024);
		fs_node_t * chd = NULL;
		if (argv[0][0] == '/') {
			memcpy(filename, argv[0], strlen(argv[0]) + 1);
			chd = kopen(filename, 0);
		}
		if (!chd) {
			/* Alright, let's try this... */
			char * search_path = "/bin/";
			memcpy(filename, search_path, strlen(search_path));
			memcpy((void*)((uintptr_t)filename + strlen(search_path)),argv[0],strlen(argv[0])+1);
			chd = kopen(filename, 0);
		}
		if (!chd) {
			kprintf("Unrecognized command: %s\n", cmd);
		} else {
			close_fs(chd);
			system(filename, tokenid, argv);
		}
		free(filename);
	}
}

uint32_t shell_cmd_cd(int argc, char * argv[]) {
	if (argc < 2) {
		return 1;
	} else {
		fs_node_t * chd = kopen(argv[1], 0);
		char * path = canonicalize_path(shell.path, argv[1]);
		if (chd) {
			if ((chd->flags & FS_DIRECTORY) == 0) {
				kprintf("%s: %s is not a directory\n", argv[0], argv[1]);
				free(chd);
				return 1;
			}
			if (shell.node != fs_root) {
				free(shell.node);
			}
			shell.node = chd;
			memcpy(shell.path, path, strlen(path) + 1);
			free(path);
			for (uint32_t i = 0; i <= strlen(shell.path); ++i) {
				current_task->wd[i] = shell.path[i];
			}
		}
	}
	return 0;
}

uint32_t shell_cmd_info(int argc, char * argv[]) {
	if (argc < 2) {
		kprintf("%s: Expected argument\n", argv[0]);
		return 1;
	}
	fs_node_t * file = kopen(argv[1], 0);
	if (!file) {
		kprintf("Could not open file `%s`\n", argv[1]);
		return 1;
	}
	kprintf("flags:   0x%x\n", file->flags);
	kprintf("mask:    0x%x\n", file->mask);
	kprintf("inode:   0x%x\n", file->inode);
	kprintf("uid: %d gid: %d\n", file->uid, file->gid);
	kprintf("open():  0x%x\n", file->open);
	kprintf("read():  0x%x\n", file->read);
	kprintf("write(): 0x%x\n", file->write);
	if ((file->mask & 0x001) || (file->mask & 0x008) || (file->mask & 0x040)) {
		kprintf("File is executable.\n");
	}
	close_fs(file);
	return 0;
}

uint32_t shell_cmd_ls(int argc, char * argv[]) {
	/*
	 * List the files in the current working directory
	 */
	struct dirent * entry = NULL;
	int i = 0;
	fs_node_t * ls_node;
	char * dir_path;
	if (argc < 2) {
		ls_node = shell.node;
		dir_path = shell.path;
	} else {
		ls_node = kopen(argv[1], 0);
		dir_path = argv[1];
		if (!ls_node) {
			kprintf("%s: Could not stat directory '%s'.\n", argv[0], argv[1]);
			return 1;
		}
	}
	entry = readdir_fs(ls_node, i);
	while (entry != NULL) {
		char * filename = malloc(sizeof(char) * 1024);
		memcpy(filename, dir_path, strlen(dir_path));
		if (!strcmp(dir_path,"/")) {
			memcpy((void *)((uintptr_t)filename + strlen(dir_path)),entry->name,strlen(entry->name)+1); 
		} else {
			filename[strlen(dir_path)] = '/';
			memcpy((void *)((uintptr_t)filename + strlen(dir_path) + 1),entry->name,strlen(entry->name)+1); 
		}
		fs_node_t * chd = kopen(filename, 0);
		if (chd) {
			if (chd->flags & FS_DIRECTORY) {
				kprintf("\033[1;34m");
			} else if ((chd->mask & 0x001) || (chd->mask & 0x008) || (chd->mask & 0x040)) {
				kprintf("\033[1;32m");
			}
			close_fs(chd);
		}
		free(filename);
		kprintf("%s\033[0m\n", entry->name);
		free(entry);
		i++;
		entry = readdir_fs(ls_node, i);
	}
	if (ls_node != shell.node) {
		close_fs(ls_node);
	}
	return 0;
}

uint32_t shell_cmd_out(int argc, char * argv[]) {
	if (argc < 3) {
		kprintf("Need a port and a character (both as numbers, please) to write...\n");
		return 1;
	} else {
		int port;
		port = atoi(argv[1]);
		int val;
		val  = atoi(argv[2]);
		kprintf("Writing %d (%c) to port %d\n", val, (unsigned char)val, port);
		outportb((short)port, (unsigned char)val);
	}
	return 0;
}

uint32_t shell_cmd_cpudetect(int argc, char * argv[]) {
	detect_cpu();
	return 0;
}

uint32_t shell_cmd_multiboot(int argc, char * argv[]) {
	dump_multiboot(mboot_ptr);
	return 0;
}

uint32_t shell_cmd_screenshot(int argc, char * argv[]) {
	if (argc < 2) {
		bochs_screenshot(NULL);
	} else {
		bochs_screenshot(argv[1]);
	}
	return 0;
}

uint32_t shell_cmd_readsb(int argc, char * argv[]) {
	extern void ext2_disk_read_superblock();
	ext2_disk_read_superblock();
	return 0;
}

uint32_t shell_cmd_readdisk(int argc, char * argv[]) {
	uint8_t buf[512] = {1};
	uint32_t i = 0;
	uint8_t slave = 0;
	if (argc >= 2) {
		if (!strcmp(argv[1], "slave")) {
			slave = 1;
		}
	}
	while (buf[0]) {
		ide_read_sector(0x1F0, slave, i, buf);
		for (uint16_t j = 0; j < 512; ++j) {
			ansi_put(buf[j]);
		}
		++i;
	}
	return 0;
}

uint32_t shell_cmd_writedisk(int argc, char * argv[]) {
	uint8_t buf[512] = "Hello world!\n";
	ide_write_sector(0x1F0, 0, 0x000000, buf);
	return 0;
}

ext2_inodetable_t * ext2_disk_alloc_inode(ext2_inodetable_t * parent, char * name);

uint32_t shell_cmd_testing(int argc, char * argv[]) {
	ext2_inodetable_t * derp = ext2_disk_alloc_inode(NULL, "test");
	free(derp);
	return 0;
}

uint32_t shell_cmd_history(int argc, char * argv[]) {
	for (size_t i = 0; i < shell_history_count; ++i) {
		kprintf("%d\t%s\n", i + 1, shell_history_get(i));
	}
	return 0;
}

uint32_t shell_cmd_testlist(int argc, char * argv[]) {
	list_t * list = list_create();
	int * i = malloc(sizeof(int));
	*i = 32;
	list_insert(list,i);
	i = malloc(sizeof(int));
	*i = 245252;
	list_insert(list,i);
	i = malloc(sizeof(int));
	*i = 6432643;
	list_insert(list,i);
	i = malloc(sizeof(int));
	*i = 9502;
	list_insert(list,i);
	kprintf("list: %d\n", list->length);
	foreach(node, list) {
		kprintf("0x%x ", node);
		kprintf("-> %d\n", *(uint32_t *)node->value);
	}
	list_remove(list, 0);
	kprintf("list: %d\n", list->length);
	foreach(node, list) {
		kprintf("0x%x ", node);
		kprintf("-> %d\n", *(uint32_t *)node->value);
	}
	list_destroy(list);
	list_free(list);
	free(list);
	return 0;
}

void debug_print_tree_node(tree_node_t * node, size_t height) {
	if (!node) return;
	for (uint32_t i = 0; i < height; ++i) { kprintf("  "); }
	kprintf("%s\n", (char *)node->value);
	foreach(child, node->children) {
		debug_print_tree_node(child->value, height + 1);
	}
}

void debug_print_tree(tree_t * tree) {
	kprintf("Tree 0x%x; %d nodes\n", tree, tree->nodes);
	debug_print_tree_node(tree->root, 0);
}

uint32_t shell_cmd_testtree(int argc, char * argv[]) {
	tree_t * tree = tree_create();
	tree_set_root(tree,"a");
	tree_node_t * b = tree_node_insert_child(tree, tree->root, "b");
	tree_node_t * c = tree_node_insert_child(tree, tree->root, "c");
	tree_node_t * d = tree_node_insert_child(tree, b, "d");
	tree_node_t * e = tree_node_insert_child(tree, c, "e");
	tree_node_insert_child(tree, c, "f");
	tree_node_insert_child(tree, e, "g");
	tree_node_insert_child(tree, d, "h");
	debug_print_tree(tree);
	tree_node_remove(tree, d);
	debug_print_tree(tree);
	tree_remove(tree, e);
	debug_print_tree(tree);
	tree_destroy(tree);
	tree_free(tree);
	free(tree);
	return 0;
}

uint32_t shell_cmd_testnewprocessmodel(int argc, char * argv[]) {
	process_t * init = debug_make_init();
	process_t * n    = spawn_process(init);
	n->name = "hello world";
		process_t * p = spawn_process(n);
		p->name = "herp";
		process_t * f = spawn_process(n);
		f->name = "derp";
	process_disown(p);

	return 0;
}

uint32_t shell_cmd_ps(int argc, char * argv[]) {
	debug_print_process_tree();
	return 0;
}

void install_commands() {
	shell_install_command("cd",         shell_cmd_cd);
	shell_install_command("ls",         shell_cmd_ls);
	shell_install_command("info",       shell_cmd_info);
	shell_install_command("out",        shell_cmd_out);
	shell_install_command("cpu-detect", shell_cmd_cpudetect);
	shell_install_command("multiboot",  shell_cmd_multiboot);
	shell_install_command("screenshot", shell_cmd_screenshot);
	shell_install_command("read-sb",    shell_cmd_readsb);
	shell_install_command("read-disk",  shell_cmd_readdisk);
	shell_install_command("write-disk", shell_cmd_writedisk);
	shell_install_command("test-alloc-block", shell_cmd_testing);
	shell_install_command("history",    shell_cmd_history);
	shell_install_command("test-list",  shell_cmd_testlist);
	shell_install_command("test-tree",  shell_cmd_testtree);
	shell_install_command("test-new-process-model", shell_cmd_testnewprocessmodel);
	shell_install_command("ps",         shell_cmd_ps);
}

void add_path_contents() {
	struct dirent * entry = NULL;
	int i = 0;
	fs_node_t * ls_node;
	ls_node = kopen("/bin", 0);
	char * dir_path = "/bin";
	if (!ls_node) {
		kprintf("Failed to open /bin\n");
		return;
	}
	entry = readdir_fs(ls_node, i);
	while (entry != NULL) {
		char * filename = malloc(sizeof(char) * 1024);
		memcpy(filename, dir_path, strlen(dir_path));
		filename[strlen(dir_path)] = '/';
		memcpy((void *)((uintptr_t)filename + strlen(dir_path) + 1),entry->name,strlen(entry->name)+1); 
		fs_node_t * chd = kopen(filename, 0);
		if (chd) {
			if (chd->flags & FS_DIRECTORY) {
			} else if ((chd->mask & 0x001) || (chd->mask & 0x008) || (chd->mask & 0x040)) {
				char * s = malloc(sizeof(char) * (strlen(entry->name) + 1));
				memcpy(s, entry->name, strlen(entry->name) + 1);
				shell_install_command(s, NULL);
			}
			close_fs(chd);
		}
		free(filename);
		free(entry);
		i++;
		entry = readdir_fs(ls_node, i);
	}
	if (ls_node != shell.node) {
		close_fs(ls_node);
	}
}

void tab_complete_shell(char * buffer) {
	char buf[1024];
	memcpy(buf, buffer, 1024);
	char * pch;
	char * cmd;
	char * save;
	pch = strtok_r(buf," ",&save);
	cmd = pch;
	char * argv[1024]; /* Command tokens (space-separated elements) */
	int argc = 0;
	if (!cmd) { 
		argv[0] = "";
		argc = 1;
	} else {
		while (pch != NULL) {
			argv[argc] = (char *)pch;
			++argc;
			pch = strtok_r(NULL," ",&save);
		}
	}
	argv[argc] = NULL;
	if (argc < 2) {
		if (buffer[strlen(buffer)-1] == ' ' || argc == 0) {
			kprintf("\n");
			for (uint32_t i = 0; i < shell_commands_len; ++i) {
				kprintf(shell_commands[i]);
				if (i < shell_commands_len - 1) {
					kprintf(", ");
				}
			}
			kprintf("\n");
			redraw_shell();
			kgets_redraw_buffer();
			return;
		} else {
			uint32_t count = 0, j = 0;
			for (uint32_t i = 0; i < shell_commands_len; ++i) {
				if (startswith(shell_commands[i], argv[0])) {
					count++;
				}
			}
			if (count == 1) {
				for (uint32_t i = 0; i < shell_commands_len; ++i) {
					if (startswith(shell_commands[i], argv[0])) {
						for (uint32_t j = 0; j < strlen(buffer); ++j) {
							kprintf("\x08 \x08");
						}
						kprintf(shell_commands[i]);
						memcpy(buffer, shell_commands[i], strlen(shell_commands[i]) + 1);
						return;
					}
				}
			}
			kprintf("\n");
			for (uint32_t i = 0; i < shell_commands_len; ++i) {
				if (startswith(shell_commands[i], argv[0])) {
					kprintf(shell_commands[i]);
					++j;
					if (j < count) {
						kprintf(", ");
					}
				}
			}
			kprintf("\n");
			redraw_shell();
			kgets_redraw_buffer();
			return;
		}
	} else {
		/* Complete path names */
		kprintf("%d\n", argc);
	}
}

void key_up_shell(char * buffer) {
	if (shell_scroll == 0) {
		memcpy(shell_temp, buffer, strlen(buffer) + 1);
	}
	if (shell_scroll < shell_history_count) {
		shell_scroll++;
		for (size_t i = 0; i < strlen(buffer); ++i) {
			kprintf("\x08 \x08");
		}
		char * h = shell_history_prev(shell_scroll);
		memcpy(buffer, h, strlen(h) + 1);
		kprintf(h);
	}
}

void key_down_shell(char * buffer) {
	if (shell_scroll > 1) {
		shell_scroll--;
		for (size_t i = 0; i < strlen(buffer); ++i) {
			kprintf("\x08 \x08");
		}
		char * h = shell_history_prev(shell_scroll);
		memcpy(buffer, h, strlen(h) + 1);
		kprintf(h);
	} else if (shell_scroll == 1) {
		for (size_t i = 0; i < strlen(buffer); ++i) {
			kprintf("\x08 \x08");
		}
		shell_scroll = 0;
		memcpy(buffer, shell_temp, strlen(shell_temp) + 1);
		kprintf(buffer);
	}
}

void
start_shell() {
	init_shell();
	install_commands();
	add_path_contents();
	while (1) {
		/* Read buffer */
		shell_update_time();
		redraw_shell();
		char buffer[1024];
		int size;
		/* Read commands */
		kgets_redraw_func = redraw_shell;
		kgets_tab_complete_func = tab_complete_shell;
		kgets_key_down = key_down_shell;
		kgets_key_up   = key_up_shell;
		size = kgets((char *)&buffer, 1023);
		if (size < 1) {
			continue;
		} else {
			/* Execute command */
			shell_exec(buffer, size);
			shell_scroll = 0;
		}
	}
}

