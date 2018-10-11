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
#include <kernel/multiboot.h>
#include <kernel/pci.h>
#include <kernel/mod/procfs.h>

#define PROCFS_STANDARD_ENTRIES (sizeof(std_entries) / sizeof(struct procfs_entry))
#define PROCFS_PROCDIR_ENTRIES  (sizeof(procdir_entries) / sizeof(struct procfs_entry))

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

static size_t calculate_memory_usage(page_directory_t * src) {
	size_t pages = 0;
	for (uint32_t i = 0; i < 1024; ++i) {
		if (!src->tables[i] || (uintptr_t)src->tables[i] == (uintptr_t)0xFFFFFFFF) {
			continue;
		}
		if (kernel_directory->tables[i] == src->tables[i]) {
			continue;
		}
		/* For each table */
		if (i * 0x1000 * 1024 < SHM_START) {
			/* Ignore shared memory for now */
			for (int j = 0; j < 1024; ++j) {
				/* For each frame in the table... */
				if (!src->tables[i]->pages[j].frame) {
					continue;
				}
				pages++;
			}
		}
	}
	return pages;
}

static size_t calculate_shm_resident(page_directory_t * src) {
	size_t pages = 0;
	for (uint32_t i = 0; i < 1024; ++i) {
		if (!src->tables[i] || (uintptr_t)src->tables[i] == (uintptr_t)0xFFFFFFFF) {
			continue;
		}
		if (kernel_directory->tables[i] == src->tables[i]) {
			continue;
		}
		if (i * 0x1000 * 1024 < SHM_START) {
			continue;
		}
		for (int j = 0; j < 1024; ++j) {
			/* For each frame in the table... */
			if (!src->tables[i]->pages[j].frame) {
				continue;
			}
			pages++;
		}
	}
	return pages;
}

static uint32_t proc_status_func(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	char buf[2048];
	process_t * proc = process_from_pid(node->inode);
	process_t * parent = process_get_parent(proc);

	if (!proc) {
		/* wat */
		return 0;
	}

	char state = proc->finished ? 'Z' : (process_is_ready(proc) ? 'R' : 'S');
	char * name = proc->name + strlen(proc->name) - 1;

	while (1) {
		if (*name == '/') {
			name++;
			break;
		}
		if (name == proc->name) break;
		name--;
	}

	/* Calculate process memory usage */
	int mem_usage = calculate_memory_usage(proc->thread.page_directory) * 4;
	int shm_usage = calculate_shm_resident(proc->thread.page_directory) * 4;
	int mem_permille = 1000 * (mem_usage + shm_usage) / memory_total();

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
			"UserStack:\t0x%x\n"
			"Path:\t%s\n"
			"VmSize:\t %d kB\n"
			"RssShmem:\t %d kB\n"
			"MemPermille:\t %d\n"
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
			proc->syscall_registers ? proc->syscall_registers->useresp : 0,
			proc->cmdline ? proc->cmdline[0] : "(none)",
			mem_usage, shm_usage, mem_permille
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

static uint32_t pat_func(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	char buf[1024];

	uint64_t pat_values;
	asm volatile ( "rdmsr" : "=A" (pat_values) : "c" (0x277) );

	char * pat_names[] = {
		"uncacheable (UC)",
		"write combining (WC)",
		"Reserved",
		"Reserved",
		"write through (WT)",
		"write protected (WP)",
		"write back (WB)",
		"uncached (UC-)"
	};

	int pa_0 = (pat_values >>  0) & 0x7;
	int pa_1 = (pat_values >>  8) & 0x7;
	int pa_2 = (pat_values >> 16) & 0x7;
	int pa_3 = (pat_values >> 24) & 0x7;
	int pa_4 = (pat_values >> 32) & 0x7;
	int pa_5 = (pat_values >> 40) & 0x7;
	int pa_6 = (pat_values >> 48) & 0x7;
	int pa_7 = (pat_values >> 56) & 0x7;

	sprintf(buf,
			"PA0: %d %s\n"
			"PA1: %d %s\n"
			"PA2: %d %s\n"
			"PA3: %d %s\n"
			"PA4: %d %s\n"
			"PA5: %d %s\n"
			"PA6: %d %s\n"
			"PA7: %d %s\n",
			pa_0, pat_names[pa_0],
			pa_1, pat_names[pa_1],
			pa_2, pat_names[pa_2],
			pa_3, pat_names[pa_3],
			pa_4, pat_names[pa_4],
			pa_5, pat_names[pa_5],
			pa_6, pat_names[pa_6],
			pa_7, pat_names[pa_7]
	);

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

	memcpy(buffer, buf + offset, size);
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
	if (offset > _bsize) {
		free(buf);
		return 0;
	}
	if (size > _bsize - offset) size = _bsize - offset;

	memcpy(buffer, buf + offset, size);
	free(buf);
	return size;
}

static uint32_t modules_func(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	list_t * hash_keys = hashmap_keys(modules_get_list());
	char * buf = malloc(hash_keys->length * 512);
	unsigned int soffset = 0;
	foreach(_key, hash_keys) {
		char * key = (char *)_key->value;
		module_data_t * mod_info = hashmap_get(modules_get_list(), key);

		soffset += sprintf(&buf[soffset], "0x%x {.init=0x%x, .fini=0x%x} %s",
				mod_info->bin_data,
				mod_info->mod_info->initialize,
				mod_info->mod_info->finalize,
				mod_info->mod_info->name);

		if (mod_info->deps) {
			unsigned int i = 0;
			soffset += sprintf(&buf[soffset], " Deps: ");
			while (i < mod_info->deps_length) {
				/* Skip padding bytes */
				if (strlen(&mod_info->deps[i])) {
					soffset += sprintf(&buf[soffset], "%s ", &mod_info->deps[i]);
				}
				i += strlen(&mod_info->deps[i]) + 1;
			}
		}

		soffset += sprintf(&buf[soffset], "\n");
	}
	free(hash_keys);

	size_t _bsize = strlen(buf);
	if (offset > _bsize) {
		free(buf);
		return 0;
	}
	if (size > _bsize - offset) size = _bsize - offset;

	memcpy(buffer, buf + offset, size);
	free(buf);
	return size;
}

extern hashmap_t * fs_types; /* from kernel/fs/vfs.c */

static uint32_t filesystems_func(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	list_t * hash_keys = hashmap_keys(fs_types);
	char * buf = malloc(hash_keys->length * 512);
	unsigned int soffset = 0;
	foreach(_key, hash_keys) {
		char * key = (char *)_key->value;
		soffset += sprintf(&buf[soffset], "%s\n", key);
	}
	free(hash_keys);

	size_t _bsize = strlen(buf);
	if (offset > _bsize) {
		free(buf);
		return 0;
	}
	if (size > _bsize - offset) size = _bsize - offset;

	memcpy(buffer, buf + offset, size);
	free(buf);
	return size;
}

static uint32_t loader_func(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	char * buf = malloc(512);

	if (mboot_ptr->flags & MULTIBOOT_FLAG_LOADER) {
		sprintf(buf, "%s\n", mboot_ptr->boot_loader_name);
	} else {
		buf[0] = '\n';
		buf[1] = '\0';
	}

	size_t _bsize = strlen(buf);
	if (offset > _bsize) {
		free(buf);
		return 0;
	}
	if (size > _bsize - offset) size = _bsize - offset;

	memcpy(buffer, buf + offset, size);
	free(buf);
	return size;
}

extern char * get_irq_handler(int irq, int chain);

static uint32_t irq_func(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	char * buf = malloc(4096);
	unsigned int soffset = 0;

	for (int i = 0; i < 16; ++i) {
		soffset += sprintf(&buf[soffset], "irq %d: ", i);
		for (int j = 0; j < 4; ++j) {
			char * t = get_irq_handler(i, j);
			if (!t) break;
			soffset += sprintf(&buf[soffset], "%s%s", j ? "," : "", t);
		}
		soffset += sprintf(&buf[soffset], "\n");
	}

	size_t _bsize = strlen(buf);
	if (offset > _bsize) {
		free(buf);
		return 0;
	}
	if (size > _bsize - offset) size = _bsize - offset;

	memcpy(buffer, buf + offset, size);
	free(buf);
	return size;
}

/**
 * Basically the same as the kdebug `pci` command.
 */
struct _pci_buf {
	size_t   offset;
	char *buffer;
};

static void scan_hit_list(uint32_t device, uint16_t vendorid, uint16_t deviceid, void * extra) {

	struct _pci_buf * b = extra;

	b->offset += sprintf(b->buffer + b->offset, "%2x:%2x.%d (%4x, %4x:%4x) %s %s\n",
			(int)pci_extract_bus(device),
			(int)pci_extract_slot(device),
			(int)pci_extract_func(device),
			(int)pci_find_type(device),
			vendorid,
			deviceid,
			pci_vendor_lookup(vendorid),
			pci_device_lookup(vendorid,deviceid));

	b->offset += sprintf(b->buffer + b->offset, " BAR0: 0x%8x", pci_read_field(device, PCI_BAR0, 4));
	b->offset += sprintf(b->buffer + b->offset, " BAR1: 0x%8x", pci_read_field(device, PCI_BAR1, 4));
	b->offset += sprintf(b->buffer + b->offset, " BAR2: 0x%8x", pci_read_field(device, PCI_BAR2, 4));
	b->offset += sprintf(b->buffer + b->offset, " BAR3: 0x%8x", pci_read_field(device, PCI_BAR3, 4));
	b->offset += sprintf(b->buffer + b->offset, " BAR4: 0x%8x", pci_read_field(device, PCI_BAR4, 4));
	b->offset += sprintf(b->buffer + b->offset, " BAR6: 0x%8x\n", pci_read_field(device, PCI_BAR5, 4));

	b->offset += sprintf(b->buffer + b->offset, " IRQ Line: %d", pci_read_field(device, 0x3C, 1));
	b->offset += sprintf(b->buffer + b->offset, " IRQ Pin: %d", pci_read_field(device, 0x3D, 1));
	b->offset += sprintf(b->buffer + b->offset, " Interrupt: %d", pci_get_interrupt(device));
	b->offset += sprintf(b->buffer + b->offset, " Status: 0x%4x\n", pci_read_field(device, PCI_STATUS, 2));
}

static void scan_count(uint32_t device, uint16_t vendorid, uint16_t deviceid, void * extra) {
	size_t * count = extra;
	(*count)++;
}

static uint32_t pci_func(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	size_t count = 0;
	pci_scan(&scan_count, -1, &count);

	struct _pci_buf b = {0,NULL};
	b.buffer = malloc(count * 1024);

	pci_scan(&scan_hit_list, -1, &b);

	size_t _bsize = b.offset;
	if (offset > _bsize) {
		free(b.buffer);
		return 0;
	}
	if (size > _bsize - offset) size = _bsize - offset;

	memcpy(buffer, b.buffer + offset, size);
	free(b.buffer);
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
	{-8, "modules",  modules_func},
	{-9, "filesystems", filesystems_func},
	{-10,"loader",   loader_func},
	{-11,"irq",      irq_func},
	{-12,"pat",      pat_func},
	{-13,"pci",      pci_func},
};

static list_t * extended_entries = NULL;
static int next_id = 0;

int procfs_install(struct procfs_entry * entry) {
	if (!extended_entries) {
		extended_entries = list_create();
		next_id = -PROCFS_STANDARD_ENTRIES - 1;
	}

	entry->id = next_id--;
	list_insert(extended_entries, entry);

	return 0;
}

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

	index -= PROCFS_STANDARD_ENTRIES;

	if (extended_entries) {
		if (index < extended_entries->length) {
			size_t i = 0;
			node_t * n = extended_entries->head;
			while (i < index) {
				n = n->next;
				i++;
			}

			struct procfs_entry * e = n->value;
			struct dirent * out = malloc(sizeof(struct dirent));
			memset(out, 0x00, sizeof(struct dirent));
			out->ino = e->id;
			strcpy(out->name, e->name);
			return out;
		}
		index -=  extended_entries->length;
	}

	int i = index + 1;

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

static int readlink_self(fs_node_t * node, char * buf, size_t size) {
	char tmp[30];
	size_t req;
	sprintf(tmp, "/proc/%d", current_process->id);
	req = strlen(tmp) + 1;

	if (size < req) {
		memcpy(buf, tmp, size);
		buf[size-1] = '\0';
		return size-1;
	}

	if (size > req) size = req;

	memcpy(buf, tmp, size);
	return size-1;
}

static fs_node_t * procfs_create_self(void) {
	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	memset(fnode, 0x00, sizeof(fs_node_t));
	fnode->inode = 0;
	strcpy(fnode->name, "self");
	fnode->mask = 0777;
	fnode->uid  = 0;
	fnode->gid  = 0;
	fnode->flags   = FS_FILE | FS_SYMLINK;
	fnode->readlink = readlink_self;
	fnode->length  = 1;
	fnode->nlink   = 1;
	fnode->ctime   = now();
	fnode->mtime   = now();
	fnode->atime   = now();
	return fnode;
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
		return procfs_create_self();
	}

	for (unsigned int i = 0; i < PROCFS_STANDARD_ENTRIES; ++i) {
		if (!strcmp(name, std_entries[i].name)) {
			fs_node_t * out = procfs_generic_create(std_entries[i].name, std_entries[i].func);
			return out;
		}
	}

	foreach(node, extended_entries) {
		struct procfs_entry * e = node->value;
		if (!strcmp(name, e->name)) {
			fs_node_t * out = procfs_generic_create(e->name, e->func);
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
