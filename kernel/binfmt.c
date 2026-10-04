/**
 * @file  kernel/binfmt.c
 * @brief Top-level executable parsing.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
 */
#include <bits/errno.h>
#include <kernel/vfs.h>
#include <kernel/printf.h>
#include <kernel/process.h>
#include <kernel/string.h>
#include <kernel/mmu.h>
#include <kernel/elf.h>
#include <sys/time.h>

extern int elf_exec(const char *, struct fs_file_description *, int argc, char * argv[], char * env[], int interp);
int exec(const char * path, int argc, char * argv[], char * env[], int interp_depth);

void binfmt_exec_cleanup(struct fs_file_description *desc_in, int argc, char **argv, char **env) {
	for (int i = 0; i < argc; i++) free(argv[i]);
	free(argv);
	for (char ** a = env; *a; a++) free(*a);
	free(env);
	if (desc_in) fs_close_desc(desc_in);
}

/**
 * @brief hash-exclamation parser
 *
 * Tries to safely read the first line of a script file to find an appropriate loader.
 */
int exec_shebang(const char * path, struct fs_file_description * desc_in, int argc, char * argv[], char * env[], int interp) {
	int error = 0;
	if (interp > 4) {
		error = -ELOOP;
		goto _free_most;
	}

	/* Read MAX_LINE... */
	char tmp[101] = {0};
	error = read_fs(desc_in->inode, 0, 100, (unsigned char *)tmp);
	if (error < 0) goto _free_most;
	char * cmd = (char *)&tmp[2];
	if (*cmd == ' ') cmd++; /* Handle a leading space */
	char * space_or_linefeed = strpbrk(cmd, " \n");
	char * arg = NULL;

	/* We read too much stuff before finding EOL or another signal
	 * that the interpreter was found, so bail. */
	if (!space_or_linefeed) {
		error = -ENOEXEC;
		goto _free_most;
	}

	/* If we found a space, accept one argument before the path... */
	if (*space_or_linefeed == ' ') {
		*space_or_linefeed = '\0';
		space_or_linefeed++;
		arg = space_or_linefeed;
		/* ... and look for another EOL. */
		space_or_linefeed = strpbrk(space_or_linefeed, "\n");
		if (!space_or_linefeed) {
			error = -ENOEXEC;
			goto _free_most;
		}
	}

	/* Make sure interpreter or argument is nil-terminated */
	*space_or_linefeed = '\0';

	char * script = strdup(path);
	fs_close_desc(desc_in);

	unsigned int nargc = argc + (arg ? 2 : 1);
	char ** args = calloc(nargc + 2, sizeof(char*));
	args[0] = strdup(cmd);
	args[1] = arg ? strdup(arg) : script;
	args[2] = arg ? script : NULL;
	args[3] = NULL;

	int j = arg ? 3 : 2;
	for (int i = 1; i < argc; ++i, ++j) {
		args[j] = argv[i];
	}
	args[j] = NULL;

	free(argv[0]);
	free(argv);

	/* Try to execut the interpreter with the new arguments */
	return exec(args[0], nargc, args, env, interp+1);

_free_most:
	binfmt_exec_cleanup(desc_in, argc, argv, env);
	return error;
}

/* Consider exposing this and making it a list so it can be extended ... */
typedef int (*exec_func)(const char *, struct fs_file_description*, int argc, char * argv[], char * env[], int interp);
typedef struct {
	exec_func func;
	unsigned char bytes[4];
	unsigned int  match;
	const char * name;
} exec_def_t;

exec_def_t fmts[] = {
	{elf_exec, {ELFMAG0, ELFMAG1, ELFMAG2, ELFMAG3}, 4, "ELF"},
	{exec_shebang, {'#', '!', 0, 0}, 2, "#!"},
};

static int matches(unsigned char * a, unsigned char * b, unsigned int len) {
	for (unsigned int i = 0; i < len; ++i) {
		if (a[i] != b[i]) return 0;
	}
	return 1;
}

static int exec_common(struct fs_file_description * desc, const char * path, int argc, char * argv[], char * env[], int interp_depth) {
	unsigned char head[4];
	read_fs(desc->inode, 0, 4, head);

	if (interp_depth == 0) {
		if (this_core->current_process->name) free(this_core->current_process->name);
		this_core->current_process->name = strdup(fs_basename(path));
	}

	for (unsigned int i = 0; i < sizeof(fmts) / sizeof(exec_def_t); ++i) {
		if (matches(fmts[i].bytes, head, fmts[i].match)) {
			return fmts[i].func(path, desc, argc, argv, env, interp_depth);
		}
	}

	binfmt_exec_cleanup(desc, argc, argv, env);
	return -ENOEXEC;
}

/**
 * @brief Replace the current process with a new one.
 *
 * @param path Filename of the new executable.
 * @param argc Number of arguments passed in @p argv
 * @param argv Arguments to supply to the new executable's entry point.
 * @param env  Environment strings to pass to the new executable.
 * @param interp_depth Should be 0 for all external callers.
 * @returns Either never or -ENOEXEC on failure.
 */
int exec(const char * path, int argc, char * argv[], char * env[], int interp_depth) {
	int error = 0;
	struct fs_file_description * desc = kopen_at(this_core->current_process->wd, path, O_PATH, 0, &error);
	if (!desc) goto _free_args;

	if (!has_permission(desc->inode, X_OK)) {
		error = EACCES;
		goto _free_args;
	}
	if (desc->inode->type == INO_DIR) {
		error = EISDIR;
		goto _free_args;
	}

	return exec_common(desc, path, argc, argv, env, interp_depth);

_free_args:
	binfmt_exec_cleanup(desc, argc, argv, env);
	return -error;
}

/**
 * @brief Replace current process with a new one, from an open file description.
 */
int fexec(struct fs_file_description * desc, int argc, char * argv[], char * env[]) {
	if (desc->inode->type == INO_DIR) {
		binfmt_exec_cleanup(NULL, argc, argv, env);
		return -EISDIR;
	}
	fs_clone_desc(desc, 0); /* desc is from fd table which will be wiped, obtain a new reference */

	return exec_common(desc, desc->path->chars, argc, argv, env, 0);
}

