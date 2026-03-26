/**
 * @brief pstree - Display a tree of running process
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014-2018 K. Lange
 */
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <toaru/list.h>
#include <toaru/tree.h>
#include <toaru/procfs.h>

uint8_t find_pid(void * proc_v, void * pid_v) {
	p_t * p = proc_v;
	pid_t i = (pid_t)(uintptr_t)pid_v;

	return (uint8_t)(p->pid == i);
}

void lines_set(char **lines, size_t * lines_size, size_t i) {
	if (i >= *lines_size) {
		*lines_size = (*lines_size) * 2;
		*lines = realloc(*lines, *lines_size);
	}

	(*lines)[i] = 1;
}

void lines_clear(char **lines, size_t * lines_size, size_t from, size_t until) {
	for (; from < *lines_size && from < until; ++from) {
		(*lines)[from] = 0;
	}
}

int lines_get(char ** lines, size_t * lines_size, size_t i) {
	if (i >= *lines_size) return 0;
	return (*lines)[i];
}

void print_process_tree_node(tree_node_t * node, size_t depth, int indented, int more, char ** lines, size_t * lines_size, int show_pids) {

	p_t * proc = node->value;

	size_t depth_in = depth;
	lines_clear(lines, lines_size, depth_in, *lines_size);

	if (!indented && depth) {
		if (more) {
			printf("─┬─");
			lines_set(lines,lines_size,depth+1);
		} else {
			printf("───");
		}
		depth += 3;
	} else if (depth) {
		for (int i = 0; i < (int)depth; ++i) {
			if (lines_get(lines,lines_size,i)) {
				printf("│");
			} else {
				printf(" ");
			}
		}
		if (more) {
			printf(" ├─");
			lines_set(lines,lines_size,depth+1);
		} else {
			printf(" └─");
		}
		depth += 3;
	}

	if (proc->user_data) printf("\033[1m");
	depth += printf(proc->name);

	if (show_pids) {
		depth += printf("(%d)", proc->pid);
	}

	if (proc->user_data) printf("\033[0m");

	if (!node->children->length) {
		printf("\n");
	} else {

		int t = 0;
		foreach(child, node->children) {
			/* Recursively print the children */
			print_process_tree_node(child->value, depth, !!(t), ((t+1)!=(int)node->children->length), lines, lines_size, show_pids);
			t++;
		}
	}

	lines_clear(lines, lines_size, depth_in, depth);
}

int pstree_callback(struct process * proc, void *ctx) {
	tree_t * procs = ctx;

	if (proc->ppid == 0 && proc->pid == 1) {
		tree_set_root(procs, proc);
	} else {
		tree_node_t * parent = tree_find(procs,(void *)(uintptr_t)proc->ppid,find_pid);
		if (parent) {
			tree_node_insert_child(procs, parent, proc);
		}
	}

	return 0;
}

int usage(char * argv[]) {
#define X_S "\033[3m"
#define X_E "\033[0m"
	fprintf(stderr,
			"%s - display a tree of running processes\n"
			"\n"
			"usage: %s [-p] [-T] [" X_S "pid" X_E "]\n"
			"\n"
			" --help    " X_S "Show this help message." X_E "\n"
			" -p        " X_S "Show pids." X_E "\n"
			" -T        " X_S "Hide threads." X_E "\n"
			" -h        " X_S "Hilight the current process and its ancestors." X_E "\n"
			" -H " X_S "PID    Like above, but for the given PID." X_E "\n"
			"\n",
			argv[0], argv[0]);
	return 1;
}

int main (int argc, char * argv[]) {

	int opt;
	int hide_threads = 0;
	int show_pids = 0;
	int root_pid = 1;
	int hilight_pid = 0;

	while ((opt = getopt(argc, argv, "TphH:-:")) != -1) {
		switch (opt) {
			case 'p':
				show_pids = 1;
				break;
			case 'T':
				hide_threads = 1;
				break;
			case 'h':
				hilight_pid = getpid();
				break;
			case 'H':
				hilight_pid = atoi(optarg);
				break;
			case '-':
				if (!strcmp(optarg,"help")) {
					usage(argv);
					return 0;
				}
				fprintf(stderr, "%s: '--%s' is not a recognized long option.\n", argv[0],optarg);
				/* fallthrough */
			case '?':
				return usage(argv);
		}
	}

	if (optind != argc) {
		root_pid = atoi(argv[optind]);
		optind++;
	}

	if (optind != argc) {
		return usage(argv);
	}

	/* Read the entries in the directory */
	tree_t * procs = tree_create();

	procfs_iterate(pstree_callback, procs, PROCFSLIB_NO_FREE | (hide_threads ? PROCFSLIB_NO_THREADS : 0));

	if (hilight_pid) {
		tree_node_t * proc = tree_find(procs, (void*)(uintptr_t)hilight_pid, find_pid);
		while (proc) {
			struct process * p = proc->value;
			p->user_data = 1;
			proc = proc->parent;
		}
	}

	size_t lines_size = 100;
	char * lines = calloc(lines_size,1);

	tree_node_t * root = root_pid == 1 ? procs->root : tree_find(procs,(void *)(uintptr_t)root_pid,find_pid);

	if (root) print_process_tree_node(root, 0, 0, 0, &lines, &lines_size, show_pids);

	return 0;
}

