/*
 * ToAruOS Kernel Shell
 */
#include <system.h>
#include <fs.h>

void
start_shell() {
	char path[1024];
	fs_node_t * node = fs_root;
	path[0] = '/';
	path[1] = '\0';
	while (1) {
		char buffer[1024];
		int size;
		kprintf("kernel %s> ", path);
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
			char * argv[1024];
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
						node = chd;
						memcpy(path, filename, strlen(filename));
						path[strlen(filename)] = '\0';
					} else {
						kprintf("cd: could not change directory\n");
					}
				}
			} else if (!strcmp(cmd, "cat")) {
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
						writech(bufferb[i]);
					}
					free(bufferb);
					close_fs(file);
				}
			} else if (!strcmp(cmd, "echo")) {
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
				struct dirent * entry = NULL;
				int i = 0;
				entry = readdir_fs(node, i);
				while (entry != NULL) {
					kprintf("%s\n", entry->name);
					free(entry);
					i++;
					entry = readdir_fs(node, i);
				}
			} else if (!strcmp(cmd, "help")) {
				settextcolor(9,0);
				kprintf("                 - ToAruOS Kernel Debug Shell - \n");
				resettextcolor();
				kprintf(" This is the ToAruOS kernel debugging environment.\n");
				kprintf(" From here, you have access to the virtual file system layer and \n");
				kprintf(" can read files, list files in directories, dump memory, registers,\n");
				kprintf(" and a few other things.\n");
			} else {
				kprintf("Unrecognized command: %s\n", cmd);
			}
		}
	}
}
