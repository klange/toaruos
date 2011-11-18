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
					if (!strcmp(argv[1],".")) {
						continue;
					} else {
						if (!strcmp(argv[1],"..")) {
							char * last_slash = (char *)rfind(path,'/');
							if (last_slash == path) { 
								last_slash[1] = '\0';
							} else {
								last_slash[0] = '\0';
							}
							node = kopen(path, 0);
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
						for (uint32_t i = 0; i <= strlen(path); ++i) {
							current_task->wd[i] = path[i];
						}
					}
				}
			} else if (!strcmp(cmd, "ls")) {
				/*
				 * List the files in the current working directory
				 */
				struct dirent * entry = NULL;
				int i = 0;
				fs_node_t * ls_node;
				if (tokenid < 2) {
					ls_node = node;
				} else {
					ls_node = kopen(argv[1], 0);
					if (!ls_node) {
						kprintf("Could not stat directory '%s'.\n", argv[1]);
						continue;
					}
				}
				entry = readdir_fs(ls_node, i);
				while (entry != NULL) {
					char * filename = malloc(sizeof(char) * 1024);
					memcpy(filename, path, strlen(path));
					if (!strcmp(path,"/")) {
						memcpy((void *)((uintptr_t)filename + strlen(path)),entry->name,strlen(entry->name)+1); 
					} else {
						filename[strlen(path)] = '/';
						memcpy((void *)((uintptr_t)filename + strlen(path) + 1),entry->name,strlen(entry->name)+1); 
					}
					fs_node_t * chd = kopen(filename, 0);
					if (chd) {
						if (chd->flags & FS_DIRECTORY) {
							kprintf("\033[1;34m");
						}
						close_fs(chd);
					}
					free(filename);
					kprintf("%s\033[0m\n", entry->name);
					free(entry);
					i++;
					entry = readdir_fs(ls_node, i);
				}
				if (ls_node != node) {
					close_fs(ls_node);
				}
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
			} else if (!strcmp(cmd, "cpu-detect")) {
				detect_cpu();
			} else if (!strcmp(cmd, "multiboot")) {
				dump_multiboot(mboot_ptr);
			} else if (!strcmp(cmd, "screenshot")) {
				bochs_screenshot();
			} else if (!strcmp(cmd, "read-sb")) {
				ext2_disk_read_superblock();
			} else if (!strcmp(cmd, "read-disk")) {
				uint8_t buf[512] = {1};
				uint32_t i = 0;
				uint8_t slave = 0;
				if (tokenid >= 2) {
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
			} else if (!strcmp(cmd, "write-disk")) {
				uint8_t buf[512] = "Hello world!\n";
				ide_write_sector(0x1F0, 0, 0x000000, buf);
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
	}
}
