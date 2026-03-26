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
#include <time.h>

#include <toaru/list.h>
#include <toaru/tree.h>
#include <toaru/procfs.h>

uint8_t find_pid(void * proc_v, void * pid_v) {
	p_t * p = proc_v;
	pid_t i = (pid_t)(uintptr_t)pid_v;

	return (uint8_t)(p->pid == i);
}

enum ColorMode {
	COLOR_MODE_NONE = 0,
	COLOR_MODE_AGE = 1,
	COLOR_MODE_MINE = 2,
	COLOR_MODE_CPU = 3,
	COLOR_MODE_MEM = 4,
	COLOR_MODE_INVALID = -1,
};

struct PstreeContext {
	char * lines;
	size_t lines_size;

	int show_pids;
	enum ColorMode color_mode;
	int hide_threads;
	tree_t * procs;
};

void lines_set(struct PstreeContext * ctx, size_t i) {
	if (i >= ctx->lines_size) {
		ctx->lines_size = (ctx->lines_size) * 2;
		ctx->lines = realloc(ctx->lines, ctx->lines_size);
	}

	ctx->lines[i] = 1;
}

void lines_clear(struct PstreeContext * ctx, size_t from, size_t until) {
	for (; from < ctx->lines_size && from < until; ++from) {
		ctx->lines[from] = 0;
	}
}

int lines_get(struct PstreeContext * ctx, size_t i) {
	if (i >= ctx->lines_size) return 0;
	return ctx->lines[i];
}


void print_process_tree_node(struct PstreeContext * ctx, tree_node_t * node, size_t depth, int indented, int more) {

	p_t * proc = node->value;

	size_t depth_in = depth;
	lines_clear(ctx, depth_in, ctx->lines_size);

	if (!indented && depth) {
		if (more) {
			printf("─┬─");
			lines_set(ctx, depth+1);
		} else {
			printf("───");
		}
		depth += 3;
	} else if (depth) {
		for (int i = 0; i < (int)depth; ++i) {
			if (lines_get(ctx, i)) {
				printf("│");
			} else {
				printf(" ");
			}
		}
		if (more) {
			printf(" ├─");
			lines_set(ctx, depth+1);
		} else {
			printf(" └─");
		}
		depth += 3;
	}

	if (proc->user_data) printf("\033[1m");
	if (ctx->color_mode == COLOR_MODE_AGE) {
		time_t now = time(NULL);
		if (now - proc->starttime < 60) {
			printf("\033[32m");
		} else if (now - proc->starttime < 60 * 60) {
			printf("\033[33m");
		} else {
			printf("\033[31m");
		}
	} else if (ctx->color_mode == COLOR_MODE_MINE) {
		if (proc->uid == getuid()) {
			printf("\033[32m");
		}
	} else if (ctx->color_mode == COLOR_MODE_CPU) {
		if (proc->cpu[0] > 1000) { /* 100% (multi-threaded apps using more than one core) */
			printf("\033[1;31m");
		} else if (proc->cpu[0] > 500) { /* 50% */
			printf("\033[31m");
		} else if (proc->cpu[0] > 50) { /* 5% */
			printf("\033[33m");
		} else if (proc->cpu[0] > 5) { /* 0.5% */
			printf("\033[32m");
		} else {
			printf("\033[34m");
		}
	} else if (ctx->color_mode == COLOR_MODE_MEM) {
		if (proc->mem > 666) {
			printf("\033[31m");
		} else if (proc->mem > 333) {
			printf("\033[33m");
		} else if (proc->mem > 10) {
			printf("\033[32m");
		} else {
			printf("\033[34m");
		}
	}

	depth += printf(proc->name);

	if (ctx->show_pids) {
		depth += printf("(%d)", proc->pid);
	}

	if (proc->user_data || ctx->color_mode) printf("\033[0m");

	if (!node->children->length) {
		printf("\n");
	} else {

		int t = 0;
		foreach(child, node->children) {
			/* Recursively print the children */
			print_process_tree_node(ctx, child->value, depth, !!(t), ((t+1)!=(int)node->children->length) );
			t++;
		}
	}

	lines_clear(ctx, depth_in, depth);
}

int pstree_callback(struct process * proc, void *_ctx) {
	struct PstreeContext * ctx = _ctx;
	tree_t * procs = ctx->procs;

	if (proc->cpu[0] > 1000) proc->cpu[0] = 1000; /* Cap this as it can sometimes go slightly over 100.0% */

	if (ctx->hide_threads && proc->pid != proc->tgid) {
		if (ctx->color_mode == COLOR_MODE_CPU) {
			tree_node_t * v = tree_find(procs,(void *)(uintptr_t)proc->tgid,find_pid);
			if (v) {
				((struct process *)v->value)->cpu[0] += proc->cpu[0];
			}
		}
		procfs_free(proc);
		return 0;
	}

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

static int parse_color_mode(char * str) {
	if (!strcasecmp(str, "age")) return COLOR_MODE_AGE;
	if (!strcasecmp(str, "mine")) return COLOR_MODE_MINE;
	if (!strcasecmp(str, "cpu")) return COLOR_MODE_CPU;
	if (!strcasecmp(str, "mem")) return COLOR_MODE_MEM;
	return -1;
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
			" -C " X_S "MODE   Color processes by a given mode:" X_E "\n"
			"    age    " X_S "New proceses are green; older processs are yellow;" X_E "\n"
			"           " X_S "oldest processes are red. (60s, 1hr, etc.)" X_E "\n"
			"    mine   " X_S "Color processes belonging to the calling user green." X_E "\n"
			"    cpu    " X_S "Color processes by core usage." X_E "\n"
			"    mem    " X_S "Color processes by memory usage." X_E "\n"
			"\n"
			"If a " X_S "pid" X_E " argument is provided, the tree will be rooted\n"
			"in that process; otherwise, the tree will be rooted in " X_S "init" X_E ".\n"
			"\n",
			argv[0], argv[0]);
	return 1;
}

int main (int argc, char * argv[]) {

	int opt;
	int root_pid = 1;
	int hilight_pid = 0;

	struct PstreeContext ctx = {0};

	while ((opt = getopt(argc, argv, "TphH:C:-:")) != -1) {
		switch (opt) {
			case 'p':
				ctx.show_pids = 1;
				break;
			case 'T':
				ctx.hide_threads = 1;
				break;
			case 'h':
				hilight_pid = getpid();
				break;
			case 'H':
				hilight_pid = atoi(optarg);
				break;
			case 'C':
				if ((ctx.color_mode = parse_color_mode(optarg)) == -1) return usage(argv);
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
	ctx.procs = tree_create();

	int flags = PROCFSLIB_NO_FREE;
	if (ctx.color_mode == COLOR_MODE_AGE) flags |= PROCFSLIB_COLLECT_STARTTIME;

	procfs_iterate(pstree_callback,&ctx, flags);

	if (hilight_pid) {
		tree_node_t * proc = tree_find(ctx.procs, (void*)(uintptr_t)hilight_pid, find_pid);
		while (proc) {
			struct process * p = proc->value;
			p->user_data = 1;
			proc = proc->parent;
		}
	}

	ctx.lines_size = 100;
	ctx.lines = calloc(ctx.lines_size,1);

	tree_node_t * root = root_pid == 1 ? ctx.procs->root : tree_find(ctx.procs,(void *)(uintptr_t)root_pid,find_pid);

	if (root) print_process_tree_node(&ctx, root, 0, 0, 0);

	return 0;
}

