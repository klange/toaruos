/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014 Kevin Lange
 */
#include <system.h>
#include <logging.h>
#include <fs.h>
#include <version.h>
#include <process.h>
#include <printf.h>
#include <module.h>

#define PROCFS_STANDARD_ENTRIES (sizeof(std_entries) / sizeof(struct procfs_entry))
#define PROCFS_PROCDIR_ENTRIES  (sizeof(procdir_entries) / sizeof(struct procfs_entry))

struct procfs_entry {
	int          id;
	char *       name;
	read_type_t  func;
};

static fs_node_t * procfs_generic_create(char * name, read_type_t read_func) {
	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	memset(fnode, 0x00, sizeof(fs_node_t));
	fnode->inode = 0;
	strcpy(fnode->name, name);
	fnode->uid = 0;
	fnode->gid = 0;
	fnode->mask    = 0444;
	fnode->flags   = FS_FILE;
	fnode->read    = read_func;
	fnode->write   = NULL;
	fnode->open    = NULL;
	fnode->close   = NULL;
	fnode->readdir = NULL;
	fnode->finddir = NULL;
	return fnode;
}

static uint32_t proc_cmdline_func(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	char buf[1024];
	process_t * proc = process_from_pid(node->inode);

	if (!proc) {
		/* wat */
		return 0;
	}

	if (!proc->cmdline) {
		sprintf(buf, "%s", proc->name);

		size_t _bsize = strlen(buf);
		if (offset > _bsize) return 0;
		if (size > _bsize - offset) size = _bsize - offset;

		memcpy(buffer, buf, size);
		return size;
	}


	buf[0] = '\0';

	char *  _buf = buf;
	char ** args = proc->cmdline;
	while (*args) {
		strcpy(_buf, *args);
		_buf += strlen(_buf);
		if (*(args+1)) {
			strcpy(_buf, "\036");
			_buf += strlen(_buf);
		}
		args++;
	}

	size_t _bsize = strlen(buf);
	if (offset > _bsize) return 0;
	if (size > _bsize - offset) size = _bsize - offset;

	memcpy(buffer, buf, size);
	return size;
}

static uint32_t proc_status_func(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	char buf[2048];
	process_t * proc = process_from_pid(node->inode);
	process_t * parent = process_get_parent(proc);

	if (!proc) {
		/* wat */
		return 0;
	}

	char state = process_is_ready(proc) ? 'R' : 'S';
	char * name = proc->name + strlen(proc->name) - 1;

	while (1) {
		if (*name == '/') {
			name++;
			break;
		}
		if (name == proc->name) break;
		name--;
	}

	sprintf(buf,
			"Name:\t%s\n" /* name */
			"State:\t%c\n" /* yeah, do this at some point */
			"Tgid:\t%d\n" /* group ? group : pid */
			"Pid:\t%d\n" /* pid */
			"PPid:\t%d\n" /* parent pid */
			"Uid:\t%d\n"
			,
			name,
			state,
			proc->group ? proc->group : proc->id,
			proc->id,
			parent ? parent->id : 0,
			proc->user);

	size_t _bsize = strlen(buf);
	if (offset > _bsize) return 0;
	if (size > _bsize - offset) size = _bsize - offset;

	memcpy(buffer, buf, size);
	return size;
}

static struct procfs_entry procdir_entries[] = {
	{1, "cmdline", proc_cmdline_func},
	{2, "status",  proc_status_func},
};

static struct dirent * readdir_procfs_procdir(fs_node_t *node, uint32_t index) {
	if (index == 0) {
		struct dirent * out = malloc(sizeof(struct dirent));
		memset(out, 0x00, sizeof(struct dirent));
		out->ino = 0;
		strcpy(out->name, ".");
		return out;
	}

	if (index == 1) {
		struct dirent * out = malloc(sizeof(struct dirent));
		memset(out, 0x00, sizeof(struct dirent));
		out->ino = 0;
		strcpy(out->name, "..");
		return out;
	}

	index -= 2;

	if (index < PROCFS_PROCDIR_ENTRIES) {
		struct dirent * out = malloc(sizeof(struct dirent));
		memset(out, 0x00, sizeof(struct dirent));
		out->ino = procdir_entries[index].id;
		strcpy(out->name, procdir_entries[index].name);
		return out;
	}
	return NULL;
}

static fs_node_t * finddir_procfs_procdir(fs_node_t * node, char * name) {
	if (!name) return NULL;

	for (unsigned int i = 0; i < PROCFS_PROCDIR_ENTRIES; ++i) {
		if (!strcmp(name, procdir_entries[i].name)) {
			fs_node_t * out = procfs_generic_create(procdir_entries[i].name, procdir_entries[i].func);
			out->inode = node->inode;
			return out;
		}
	}

	return NULL;
}


static fs_node_t * procfs_procdir_create(pid_t pid) {
	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	memset(fnode, 0x00, sizeof(fs_node_t));
	fnode->inode = pid;
	sprintf(fnode->name, "%d", pid);
	fnode->uid = 0;
	fnode->gid = 0;
	fnode->mask = 0555;
	fnode->flags   = FS_DIRECTORY;
	fnode->read    = NULL;
	fnode->write   = NULL;
	fnode->open    = NULL;
	fnode->close   = NULL;
	fnode->readdir = readdir_procfs_procdir;
	fnode->finddir = finddir_procfs_procdir;
	fnode->nlink   = 1;
	return fnode;
}

static uint32_t cpuinfo_func(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	return 0;
}

static uint32_t meminfo_func(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	char buf[1024];
	unsigned int total = memory_total();
	unsigned int free  = total - memory_use();
	sprintf(buf, "MemTotal: %d kB\nMemFree: %d kB\n", total, free);

	size_t _bsize = strlen(buf);
	if (offset > _bsize) return 0;
	if (size > _bsize - offset) size = _bsize - offset;

	memcpy(buffer, buf, size);
	return size;
}

static uint32_t uptime_func(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	char buf[1024];
	sprintf(buf, "%d.%3d\n", timer_ticks, timer_subticks);

	size_t _bsize = strlen(buf);
	if (offset > _bsize) return 0;
	if (size > _bsize - offset) size = _bsize - offset;

	memcpy(buffer, buf, size);
	return size;
}

static uint32_t cmdline_func(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	char buf[1024];
	extern char * cmdline;
	sprintf(buf, "%s\n", cmdline ? cmdline : "");

	size_t _bsize = strlen(buf);
	if (offset > _bsize) return 0;
	if (size > _bsize - offset) size = _bsize - offset;

	memcpy(buffer, buf, size);
	return size;
}

static uint32_t version_func(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	char buf[1024];
	char version_number[512];
	sprintf(version_number, __kernel_version_format,
			__kernel_version_major,
			__kernel_version_minor,
			__kernel_version_lower,
			__kernel_version_suffix);
	sprintf(buf, "%s %s %s %s %s %s\n",
			__kernel_name,
			version_number,
			__kernel_version_codename,
			__kernel_build_date,
			__kernel_build_time,
			__kernel_arch);

	size_t _bsize = strlen(buf);
	if (offset > _bsize) return 0;
	if (size > _bsize - offset) size = _bsize - offset;

	memcpy(buffer, buf, size);
	return size;
}

static uint32_t compiler_func(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	char buf[1024];
	sprintf(buf, "%s\n", __kernel_compiler_version);

	size_t _bsize = strlen(buf);
	if (offset > _bsize) return 0;
	if (size > _bsize - offset) size = _bsize - offset;

	memcpy(buffer, buf, size);
	return size;
}

static struct procfs_entry std_entries[] = {
	{-1, "cpuinfo",  cpuinfo_func},
	{-2, "meminfo",  meminfo_func},
	{-3, "uptime",   uptime_func},
	{-4, "cmdline",  cmdline_func},
	{-5, "version",  version_func},
	{-6, "compiler", compiler_func},
};

static struct dirent * readdir_procfs_root(fs_node_t *node, uint32_t index) {
	if (index == 0) {
		struct dirent * out = malloc(sizeof(struct dirent));
		memset(out, 0x00, sizeof(struct dirent));
		out->ino = 0;
		strcpy(out->name, ".");
		return out;
	}

	if (index == 1) {
		struct dirent * out = malloc(sizeof(struct dirent));
		memset(out, 0x00, sizeof(struct dirent));
		out->ino = 0;
		strcpy(out->name, "..");
		return out;
	}

	index -= 2;

	if (index < PROCFS_STANDARD_ENTRIES) {
		struct dirent * out = malloc(sizeof(struct dirent));
		memset(out, 0x00, sizeof(struct dirent));
		out->ino = std_entries[index].id;
		strcpy(out->name, std_entries[index].name);
		return out;
	}
	int i = index - PROCFS_STANDARD_ENTRIES + 1;

	debug_print(WARNING, "%d %d %d", i, index, PROCFS_STANDARD_ENTRIES);

	pid_t pid = 0;

	foreach(lnode, process_list) {
		i--;
		if (i == 0) {
			process_t * proc = (process_t *)lnode->value;
			pid = proc->id;
			break;
		}
	}

	if (pid == 0) {
		return NULL;
	}

	struct dirent * out = malloc(sizeof(struct dirent));
	memset(out, 0x00, sizeof(struct dirent));
	out->ino  = pid;
	sprintf(out->name, "%d", pid);

	return out;
}

static fs_node_t * finddir_procfs_root(fs_node_t * node, char * name) {
	if (!name) return NULL;
	if (strlen(name) < 1) return NULL;

	if (name[0] >= '0' && name[0] <= '9') {
		/* XXX process entries */
		pid_t pid = atoi(name);
		process_t * proc = process_from_pid(pid);
		if (!proc) {
			return NULL;
		}
		fs_node_t * out = procfs_procdir_create(pid);
		return out;
	}

	for (unsigned int i = 0; i < PROCFS_STANDARD_ENTRIES; ++i) {
		if (!strcmp(name, std_entries[i].name)) {
			fs_node_t * out = procfs_generic_create(std_entries[i].name, std_entries[i].func);
			return out;
		}
	}

	return NULL;
}


static fs_node_t * procfs_create(void) {
	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	memset(fnode, 0x00, sizeof(fs_node_t));
	fnode->inode = 0;
	strcpy(fnode->name, "proc");
	fnode->mask = 0555;
	fnode->uid  = 0;
	fnode->gid  = 0;
	fnode->flags   = FS_DIRECTORY;
	fnode->read    = NULL;
	fnode->write   = NULL;
	fnode->open    = NULL;
	fnode->close   = NULL;
	fnode->readdir = readdir_procfs_root;
	fnode->finddir = finddir_procfs_root;
	fnode->nlink   = 1;
	return fnode;
}

int procfs_initialize(void) {
	/* TODO Move this to some sort of config */
	vfs_mount("/proc", procfs_create());

	debug_print_vfs_tree();
	return 0;
}

int procfs_finalize(void) {
	return 0;
}

MODULE_DEF(procfs, procfs_initialize, procfs_finalize);
