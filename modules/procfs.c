/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014-2018 K. Lange
 */
#include <kernel/system.h>
#include <kernel/logging.h>
#include <kernel/fs.h>
#include <kernel/version.h>
#include <kernel/process.h>
#include <kernel/printf.h>
#include <kernel/module.h>
#include <kernel/mod/net.h>

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
	fnode->ctime   = now();
	fnode->mtime   = now();
	fnode->atime   = now();
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

		memcpy(buffer, buf + offset, size);
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

	memcpy(buffer, buf + offset, size);
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
			"Ueip:\t0x%x\n"
			"SCid:\t%d\n"
			"SC0:\t0x%x\n"
			"SC1:\t0x%x\n"
			"SC2:\t0x%x\n"
			"SC3:\t0x%x\n"
			"SC4:\t0x%x\n"
			"Path:\t%s\n"
			,
			name,
			state,
			proc->group ? proc->group : proc->id,
			proc->id,
			parent ? parent->id : 0,
			proc->user,
			proc->syscall_registers ? proc->syscall_registers->eip : 0,
			proc->syscall_registers ? proc->syscall_registers->eax : 0,
			proc->syscall_registers ? proc->syscall_registers->ebx : 0,
			proc->syscall_registers ? proc->syscall_registers->ecx : 0,
			proc->syscall_registers ? proc->syscall_registers->edx : 0,
			proc->syscall_registers ? proc->syscall_registers->esi : 0,
			proc->syscall_registers ? proc->syscall_registers->edi : 0,
			proc->cmdline ? proc->cmdline[0] : "(none)"
			);

	size_t _bsize = strlen(buf);
	if (offset > _bsize) return 0;
	if (size > _bsize - offset) size = _bsize - offset;

	memcpy(buffer, buf + offset, size);
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


static fs_node_t * procfs_procdir_create(process_t * process) {
	pid_t pid = process->id;
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
	fnode->ctime   = process->start.tv_sec;
	fnode->mtime   = process->start.tv_sec;
	fnode->atime   = process->start.tv_sec;
	return fnode;
}

#define cpuid(in,a,b,c,d) do { asm volatile ("cpuid" : "=a"(a),"=b"(b),"=c"(c),"=d"(d) : "a"(in)); } while(0)

static uint32_t cpuinfo_func(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	char buf[1024];

	unsigned long a, b, unused;;
	cpuid(0,unused,b,unused,unused);

	char * _manu = "Unknown";
	int _model = 0, _family = 0;

	if (b == 0x756e6547) {
		cpuid(1, a, b, unused, unused);
		_manu   = "Intel";
		_model  = (a >> 4) & 0x0F;
		_family = (a >> 8) & 0x0F;
	} else if (b == 0x68747541) {
		cpuid(1, a, unused, unused, unused);
		_manu   = "AMD";
		_model  = (a >> 4) & 0x0F;
		_family = (a >> 8) & 0x0F;
	}

	sprintf(buf,
		"Manufacturer: %s\n"
		"Family: %d\n"
		"Model: %d\n"
		, _manu, _family, _model);

	size_t _bsize = strlen(buf);
	if (offset > _bsize) return 0;
	if (size > _bsize - offset) size = _bsize - offset;

	memcpy(buffer, buf + offset, size);
	return size;
}

extern uintptr_t heap_end;
extern uintptr_t kernel_heap_alloc_point;

static uint32_t meminfo_func(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	char buf[1024];
	unsigned int total = memory_total();
	unsigned int free  = total - memory_use();
	unsigned int kheap = (heap_end - kernel_heap_alloc_point) / 1024;
	sprintf(buf,
		"MemTotal: %d kB\n"
		"MemFree: %d kB\n"
		"KHeapUse: %d kB\n"
		, total, free, kheap);

	size_t _bsize = strlen(buf);
	if (offset > _bsize) return 0;
	if (size > _bsize - offset) size = _bsize - offset;

	memcpy(buffer, buf + offset, size);
	return size;
}

static uint32_t uptime_func(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	char buf[1024];
	sprintf(buf, "%d.%3d\n", timer_ticks, timer_subticks);

	size_t _bsize = strlen(buf);
	if (offset > _bsize) return 0;
	if (size > _bsize - offset) size = _bsize - offset;

	memcpy(buffer, buf + offset, size);
	return size;
}

static uint32_t cmdline_func(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	char buf[1024];
	extern char * cmdline;
	sprintf(buf, "%s\n", cmdline ? cmdline : "");

	size_t _bsize = strlen(buf);
	if (offset > _bsize) return 0;
	if (size > _bsize - offset) size = _bsize - offset;

	memcpy(buffer, buf + offset, size);
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

	memcpy(buffer, buf + offset, size);
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

extern tree_t * fs_tree; /* kernel/fs/vfs.c */

static void mount_recurse(char * buf, tree_node_t * node, size_t height) {
	/* End recursion on a blank entry */
	if (!node) return;
	char * tmp = malloc(512);
	memset(tmp, 0, 512);
	char * c = tmp;
	/* Indent output */
	for (uint32_t i = 0; i < height; ++i) {
		c += sprintf(c, "  ");
	}
	/* Get the current process */
	struct vfs_entry * fnode = (struct vfs_entry *)node->value;
	/* Print the process name */
	if (fnode->file) {
		c += sprintf(c, "%s → %s 0x%x (%s, %s)", fnode->name, fnode->device, fnode->file, fnode->fs_type, fnode->file->name);
	} else {
		c += sprintf(c, "%s → (empty)", fnode->name);
	}
	/* Linefeed */
	sprintf(buf+strlen(buf),"%s\n",tmp);
	free(tmp);
	foreach(child, node->children) {
		/* Recursively print the children */
		mount_recurse(buf+strlen(buf),child->value, height + 1);
	}
}

static uint32_t mounts_func(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	char * buf = malloc(4096);

	buf[0] = '\0';

	mount_recurse(buf, fs_tree->root, 0);

	size_t _bsize = strlen(buf);
	if (offset > _bsize) return 0;
	if (size > _bsize - offset) size = _bsize - offset;

	memcpy(buffer, buf, size);
	free(buf);
	return size;
}

static uint32_t netif_func(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	char * buf = malloc(4096);

	/* In order to not directly depend on the network module, we dynamically locate the symbols we need. */
	void (*ip_ntoa)(uint32_t, char *) = (void (*)(uint32_t,char*))(uintptr_t)hashmap_get(modules_get_symbols(),"ip_ntoa");

	struct netif * (*get_netif)(void) = (struct netif *(*)(void))(uintptr_t)hashmap_get(modules_get_symbols(),"get_default_network_interface");

	uint32_t (*get_dns)(void) = (uint32_t (*)(void))(uintptr_t)hashmap_get(modules_get_symbols(),"get_primary_dns");

	if (get_netif) {
		struct netif * netif = get_netif();
		char ip[16];
		ip_ntoa(netif->source, ip);
		char dns[16];
		ip_ntoa(get_dns(), dns);
		char gw[16];
		ip_ntoa(netif->gateway, gw);

		if (netif->hwaddr[0] == 0 &&
			netif->hwaddr[1] == 0 &&
			netif->hwaddr[2] == 0 &&
			netif->hwaddr[3] == 0 &&
			netif->hwaddr[4] == 0 &&
			netif->hwaddr[5] == 0) {

			sprintf(buf, "no network\n");
		} else {
			sprintf(buf,
				"ip:\t%s\n"
				"mac:\t%2x:%2x:%2x:%2x:%2x:%2x\n"
				"device:\t%s\n"
				"dns:\t%s\n"
				"gateway:\t%s\n"
				,
				ip,
				netif->hwaddr[0], netif->hwaddr[1], netif->hwaddr[2], netif->hwaddr[3], netif->hwaddr[4], netif->hwaddr[5],
				netif->driver,
				dns,
				gw
			);
		}
	} else {
		sprintf(buf, "no network\n");
	}

	size_t _bsize = strlen(buf);
	if (offset > _bsize) return 0;
	if (size > _bsize - offset) size = _bsize - offset;

	memcpy(buffer, buf, size);
	free(buf);
	return size;

}

static struct procfs_entry std_entries[] = {
	{-1, "cpuinfo",  cpuinfo_func},
	{-2, "meminfo",  meminfo_func},
	{-3, "uptime",   uptime_func},
	{-4, "cmdline",  cmdline_func},
	{-5, "version",  version_func},
	{-6, "compiler", compiler_func},
	{-7, "mounts",   mounts_func},
	{-8, "netif",    netif_func},
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

	if (index == 2) {
		struct dirent * out = malloc(sizeof(struct dirent));
		memset(out, 0x00, sizeof(struct dirent));
		out->ino = 0;
		strcpy(out->name, "self");
		return out;
	}

	index -= 3;

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
		fs_node_t * out = procfs_procdir_create(proc);
		return out;
	}

	if (!strcmp(name,"self")) {
		return procfs_procdir_create((process_t *)current_process);
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
	fnode->ctime   = now();
	fnode->mtime   = now();
	fnode->atime   = now();
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
