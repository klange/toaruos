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

void
start_shell() {
	/* Current working directory */
	char path[1024] = {'/', '\0'};
	/* File system node for the working directory */
	fs_node_t * node = fs_root;
	char * username = "kernel";
	char * hostname = "toaru";
	while (1) {
		/* Read buffer */
		char buffer[1024];
		int size;
		/* Print the prompt */
		uint16_t month, day, hours, minutes, seconds;
		get_date(&month, &day);
		get_time(&hours, &minutes, &seconds);
		kprintf("\033[1m[\033[1;33m%s \033[1;32m%s \033[1;31m%d/%d \033[1;34m%d:%d:%d\033[0m \033[0m%s\033[1m]\033[0m\n\033[1;32m$\033[0m ",
				username, hostname, month, day, hours, minutes, seconds, path);
		/* Read commands */
		size = kgets((char *)&buffer, 1023);
		if (size < 1) {
			continue;
		} else {
			/*
			 * Tokenize the command
			 */
			char * pch;
			char * cmd;
			char * save;
			pch = strtok_r(buffer," ",&save);
			cmd = pch;
			if (!cmd) { continue; }
			char * argv[1024]; /* Command tokens (space-separated elements) */
			int tokenid = 0;
			while (pch != NULL) {
				argv[tokenid] = (char *)pch;
				++tokenid;
				pch = strtok_r(NULL," ",&save);
			}
			argv[tokenid] = NULL;
			/*
			 * Execute the command
			 */
			if (!strcmp(cmd, "cd")) {
				/*
				 * Change Directory
				 */
				if (tokenid < 2) {
					kprintf("cd: argument expected\n");
					continue;
				} else {
					char * filename = malloc(sizeof(char) * 1024);
					if (argv[1][0] == '/') {
						memcpy(filename, argv[1], strlen(argv[1]) + 1);
					} else {
						memcpy(filename, path, strlen(path));
						if (!strcmp(path,"/")) {
							memcpy((void *)((uintptr_t)filename + strlen(path)),argv[1],strlen(argv[1])+1); 
						} else {
							filename[strlen(path)] = '/';
							memcpy((void *)((uintptr_t)filename + strlen(path) + 1),argv[1],strlen(argv[1])+1); 
						}
					}
					fs_node_t * chd = kopen(filename, 0);
					if (chd) {
						if ((chd->flags & FS_DIRECTORY) == 0) {
							kprintf("cd: %s is not a directory\n", filename);
							continue;
						}
						node = chd;
						memcpy(path, filename, strlen(filename));
						path[strlen(filename)] = '\0';
					} else {
						kprintf("cd: could not change directory\n");
					}
				}
			} else if (!strcmp(cmd, "cat")) {
				/*
				 * Read and print content of file
				 */
				if (tokenid < 2) {
					kprintf("cat: argument expected\n");
					continue;
				} else {
					char * filename = malloc(sizeof(char) * 1024);
					if (argv[1][0] == '/') {
						memcpy(filename, argv[1], strlen(argv[1]) + 1);
					} else {
						memcpy(filename, path, strlen(path));
						if (!strcmp(path,"/")) {
							memcpy((void *)((uintptr_t)filename + strlen(path)),argv[1],strlen(argv[1])+1); 
						} else {
							filename[strlen(path)] = '/';
							memcpy((void *)((uintptr_t)filename + strlen(path) + 1),argv[1],strlen(argv[1])+1); 
						}
					}
					fs_node_t * file = kopen(filename,0);
					if (!file) {
						kprintf("cat: could not open file `%s`\n", argv[1]);
						continue;
					}
					char *bufferb = malloc(file->length + 200);
					size_t bytes_read = read_fs(file, 0, file->length, (uint8_t *)bufferb);
					size_t i = 0;
					for (i = 0; i < bytes_read; ++i) {
						ansi_put(bufferb[i]);
					}
					free(bufferb);
					close_fs(file);
				}
			} else if (!strcmp(cmd, "echo")) {
				/*
				 * Print given arguments
				 */
				if (tokenid < 2) {
					continue;
				} else {
					int i = 1;
					for (i = 1; i < tokenid; ++i) {
						kprintf("%s ", argv[i]);
					}
					kprintf("\n");
				}
			} else if (!strcmp(cmd, "ls")) {
				/*
				 * List the files in the current working directory
				 */
				struct dirent * entry = NULL;
				int i = 0;
				entry = readdir_fs(node, i);
				while (entry != NULL) {
					kprintf("%s\n", entry->name);
					free(entry);
					i++;
					entry = readdir_fs(node, i);
				}
			} else if (!strcmp(cmd, "info")) {
				kprintf("Flags: 0x%x\n", node->flags);
			} else if (!strcmp(cmd, "help")) {
				kprintf("\033[1;34m");
				kprintf("                 - ToAruOS Kernel Debug Shell - \n");
				kprintf("\033[0m");
				kprintf(" This is the ToAruOS kernel debugging environment.\n");
				kprintf(" From here, you have access to the virtual file system layer and \n");
				kprintf(" can read files, list files in directories, dump memory, registers,\n");
				kprintf(" and a few other things.\n");
			} else if (!strcmp(cmd, "out")) {
				if (tokenid < 3) {
					kprintf("Need a port and a character (both as numbers, please) to write...\n");
				} else {
					int port;
					port = atoi(argv[1]);
					int val;
					val  = atoi(argv[2]);
					kprintf("Writing %d (%c) to port %d\n", val, (unsigned char)val, port);
					outportb((short)port, (unsigned char)val);
				}
			} else if (!strcmp(cmd, "serial")) {
				if (tokenid < 2) {
					kprintf("Need some arguments.\n");
				} else {
					int i = 1;
					for (i = 1; i < tokenid; ++i) {
						int j = 0;
						for (j = 0; j < strlen(argv[i]); ++j) {
							serial_send(argv[i][j]);
							writech(argv[i][j]);
						}
						writech(' ');
					}
					serial_send('\n');
					writech('\n');
				}
			} else if (!strcmp(cmd, "clear")) {
				cls();
			} else if (!strcmp(cmd, "crash")) {
				kprintf("Going to dereference some invalid pointers.\n");
				int i = 0xFFFFFFFF;
				int j = *(int *)i;
				j = 0xDEADBEEF;
			} else if (!strcmp(cmd, "exit")) {
				kprintf("Good byte.\n");
				break;
			} else if (!strcmp(cmd, "cpu-detect")) {
				detect_cpu();
			} else if (!strcmp(cmd, "scroll")) {
				bochs_scroll();
			} else if (!strcmp(cmd, "logo")) {
				if (tokenid < 2) {
					bochs_draw_logo("/bs.bmp");
				} else {
					bochs_draw_logo(argv[1]);
				}
			} else if (!strcmp(cmd, "line")) {
				if (tokenid < 5) {
					bochs_draw_line(0,1024,0,768, 0xFFFFFF);
				} else {
					bochs_draw_line(atoi(argv[1]),atoi(argv[2]),atoi(argv[3]),atoi(argv[4]),0xFFFFFF);
				}
			} else if (!strcmp(cmd, "boredom")) {
				int x = 30;
				if (tokenid > 1) {
					x = atoi(argv[1]);
				}
				for (int derp = 0; derp < x; ++derp) {
					int x0 = krand() % 1024;
					int x1 = krand() % 1024;
					int y0 = krand() % 768;
					int y1 = krand() % 768;
					bochs_draw_line(x0,x1,y0,y1, krand() % 0xFFFFFF);
				}
			} else if (!strcmp(cmd, "test-scroll")) {
				bochs_set_y_offset(12 * 20);
			} else if (!strcmp(cmd, "reset-keyboard")) {
				/* Reset the shift/alt/ctrl statuses, in case they get messed up */
				set_kbd(0,0,0);
			} else if (!strcmp(cmd, "test-ansi")) {
				ansi_print("This is a \033[32mtest\033[0m of the \033[33mANSI\033[0m driver.\n");
				ansi_print("This is a \033[32;1mte\033[34mst\033[0m of \033[1mthe \033[33mANSI\033[0m driver.\n");
			} else if (!strcmp(cmd, "multiboot")) {
				dump_multiboot(mboot_ptr);
			} else {
				kprintf("Unrecognized command: %s\n", cmd);
			}
		}
	}
}
