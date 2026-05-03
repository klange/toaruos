/**
 * @brief Scans the process tree in /procfs to determine who is "logged in".
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2026 K. Lange
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pwd.h>
#include <sys/stat.h>
#include <toaru/tree.h>
#include <toaru/procfs.h>

struct WhoContext {
	tree_t *procs;
	list_t *logins;

	int print_header;
	int print_status;
	int print_idle;
	int only_mine;
	char * my_tty;

	struct {
		int name;
		int status;
		int line;
		int time;
		int idle;
	} column_length;
};

struct WhoLogin {
	p_t * manager;
	p_t * login_process;
	char * user_name;
	char * start;
	char * line_name;
	char * status;
	char * idle;
};

static void print_name(char ** out, uid_t user) {
	struct passwd * p = getpwuid(user);
	if (p) {
		asprintf(out, "%s", p->pw_name);
	} else {
		asprintf(out, "%d", user);
	}
	endpwent();
}

/* Trimmed from pstree_callback; just collect processes into a tree. */
uint8_t find_pid(void * proc_v, void * pid_v) {
	return (uint8_t)(((p_t*)proc_v)->pid == (pid_t)(uintptr_t)pid_v);
}
static int who_callback(struct process * proc, void *_ctx) {
	struct WhoContext * ctx = _ctx;
	tree_t * procs = ctx->procs;

	if (proc->ppid == 0 && proc->pid == 1) {
		tree_set_root(procs, proc);
	} else {
		pid_t ppid = (proc->tgid != proc->pid ? proc->tgid : proc->ppid);
		tree_node_t * parent = tree_find(procs,(void *)(uintptr_t)ppid,find_pid);
		if (parent) {
			tree_node_insert_child(procs, parent, proc);
		}
	}

	return 0;
}

static char * tty_from_procfs(char * t) {
	if (!*t) return "?";

	if (strstr(t, "/dev/") == t) {
		return t + 5;
	}

	return t;
}

static void collect_logins(struct WhoContext * ctx, tree_node_t * node) {
	p_t * proc = node->value;

	/* Only consider login processes owned by root, to avoid shenangians */
	if (proc->uid == 0) {
		/* Any of these are good candidates */
		if (!strcmp(proc->name,"login") ||
		    !strcmp(proc->name, "glogin") ||
		    !strcmp(proc->name, "live-session")) {

			foreach(child, node->children) {
				/* Potential login session */
				p_t * session = ((tree_node_t *)child->value)->value;

				if (ctx->only_mine) {
					if (!*ctx->my_tty || strcmp(ctx->my_tty, session->tty)) continue;
				}

				struct WhoLogin * this_login = calloc(1,sizeof(struct WhoLogin));

				this_login->manager = proc;
				this_login->login_process = session;

				/* NAME */
				print_name(&this_login->user_name, session->uid);
				int name_len = strlen(this_login->user_name);
				if (name_len > ctx->column_length.name) ctx->column_length.name = name_len;

				/* LINE */
				this_login->line_name = tty_from_procfs(session->tty);
				int line_len = strlen(this_login->line_name);
				if (line_len > ctx->column_length.line) ctx->column_length.line = line_len;

				this_login->status = "?";
				this_login->idle = "  ?";
				struct stat st;
				if (*session->tty && !stat(session->tty, &st)) {
					this_login->status = (st.st_mode & S_IWGRP) ? "+" : "-";
					time_t idle = time(NULL) - st.st_mtim.tv_sec;
					if (idle >= 60) {
						asprintf(&this_login->idle, "%02d:%02d", idle / 60 / 60, idle / 60);
					} else {
						this_login->idle = "  .";
					}
				}
				int idle_len = strlen(this_login->idle);
				if (idle_len > ctx->column_length.idle) ctx->column_length.idle = idle_len;

				/* TIME */
				char buf[1024];
				struct tm * tm = localtime(&session->starttime);
				strftime(buf, 1024, "%F %R", tm);
				this_login->start = strdup(buf);
				int time_len = strlen(this_login->start);
				if (time_len > ctx->column_length.time) ctx->column_length.time = time_len;

				list_insert(ctx->logins, this_login);
			}

		}
	}

	/* Drill down */
	foreach(child, node->children) {
		collect_logins(ctx, child->value);
	}
}

int main(int argc, char ** argv) {
	struct WhoContext ctx = {0};
	int opt;

	while ((opt = getopt(argc, argv, "mTuH")) != -1) {
		switch (opt) {
			case 'm': /* only current terminal */
				ctx.only_mine = 1;
				break;
			case 'T': /* show each terminal */
				ctx.print_status = 1;
				break;
			case 'u': /* show idle time */
				ctx.print_idle = 1;
				break;
			case 'H':
				ctx.print_header = 1;
				break;
		}
	}

	/* Special case "who am i" */
	if (optind + 2 == argc && !strcmp(argv[optind],"am") && !strcasecmp(argv[optind+1],"i")) {
		char * buf;
		print_name(&buf, geteuid());
		printf("%s\n", buf);
		free(buf);
		return 0;
	}

	ctx.procs = tree_create();
	ctx.logins = list_create();

	if (ctx.print_header) {
		ctx.column_length.name = 4;
		ctx.column_length.status = 1;
		ctx.column_length.line = 4;
		ctx.column_length.time = 4;
		ctx.column_length.idle = 4;
	}

	if (ctx.only_mine) {
		p_t * me = procfs_get_pid(getpid(), 0);
		ctx.my_tty = me->tty;
	}

	procfs_iterate(who_callback, &ctx, PROCFSLIB_COLLECT_STARTTIME | PROCFSLIB_NO_FREE);

	collect_logins(&ctx, ctx.procs->root);

	if (ctx.print_header) {
		printf("%*s ", ctx.column_length.name, "NAME");
		if (ctx.print_status) printf("%*s ", ctx.column_length.status, " ");
		printf("%*s ", ctx.column_length.line, "LINE");
		printf("%*s ", ctx.column_length.time, "TIME");
		if (ctx.print_idle) printf("%*s ", ctx.column_length.idle, "IDLE");
		printf("%s", "PROC");
		printf("\n");
	}

	foreach(node, ctx.logins) {
		struct WhoLogin * login = node->value;
		printf("%*s ", ctx.column_length.name, login->user_name);
		if (ctx.print_status) printf("%*s ", ctx.column_length.status, login->status);
		printf("%*s ", ctx.column_length.line, login->line_name);
		printf("%*s ", ctx.column_length.time, login->start);
		if (ctx.print_idle) printf("%*s ", ctx.column_length.idle, login->idle);
		printf("%s(%d)\n", login->login_process->name, login->login_process->tgid);
	}

	return 0;
}
