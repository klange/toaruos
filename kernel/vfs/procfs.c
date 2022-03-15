/**
 * @file  kernel/vfs/procfs.c
 * @brief Extensible file-based information interface.
 *
 * Provides /proc and its contents, which allow userspace tools
 * to query kernel status through directory and text file interfaces.
 *
 * The way this is implemented is a bit messy... every time a read
 * request happens, we generate a text blob and then try to provide
 * the part the reader actually asked for. This is susceptible to
 * corruption if the layout of data changed between two read calls.
 * We should probably be generating data on open and disposing of
 * it on call...
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014-2021 K. Lange
 */
#include <stdint.h>
#include <stddef.h>
#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/vfs.h>
#include <kernel/version.h>
#include <kernel/process.h>
#include <kernel/pci.h>
#include <kernel/procfs.h>
#include <kernel/hashmap.h>
#include <kernel/time.h>
#include <kernel/syscall.h>
#include <kernel/mmu.h>
#include <kernel/misc.h>
#include <kernel/module.h>
#include <kernel/ksym.h>

#define PROCFS_STANDARD_ENTRIES (sizeof(std_entries) / sizeof(struct procfs_entry))
#define PROCFS_PROCDIR_ENTRIES  (sizeof(procdir_entries) / sizeof(struct procfs_entry))

static fs_node_t * procfs_generic_create(const char * name, read_type_t read_func) {
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

static ssize_t proc_cmdline_func(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
	char buf[1024];
	process_t * proc = process_from_pid(node->inode);

	if (!proc) {
		/* wat */
		return 0;
	}

	if (!proc->cmdline) {
		snprintf(buf, 100, "%s", proc->name);

		size_t _bsize = strlen(buf);
		if ((size_t)offset > _bsize) return 0;
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
	if ((size_t)offset > _bsize) return 0;
	if (size > _bsize - offset) size = _bsize - offset;

	memcpy(buffer, buf + offset, size);
	return size;
}

static ssize_t proc_status_func(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
	char buf[2048];
	process_t * proc = process_from_pid(node->inode);
	process_t * parent = process_get_parent(proc);

	if (!proc) {
		/* wat */
		return 0;
	}

	char state = (proc->flags & PROC_FLAG_FINISHED) ? 'Z' :
		((proc->flags & PROC_FLAG_SUSPENDED) ? 'T' :
			(process_is_ready(proc) ? 'R' : 'S'));
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
	long mem_usage = mmu_count_user(proc->thread.page_directory->directory) * 4;
	long shm_usage = mmu_count_shm(proc->thread.page_directory->directory) * 4;
	long mem_permille = 1000 * (mem_usage + shm_usage) / mmu_total_memory();

	snprintf(buf, 2000,
			"Name:\t%s\n"  /* name */
			"State:\t%c\n"
			"Tgid:\t%d\n"  /* group ? group : pid */
			"Pid:\t%d\n"   /* pid */
			"PPid:\t%d\n"  /* parent pid */
			"Pgid:\t%d\n"  /* progress group id (job) */
			"Sid:\t%d\n"   /* session id */
			"Uid:\t%d\n"
			"Ueip:\t%#zx\n"
			"SCid:\t%zu\n"
			"SC0:\t%#zx\n"
			"SC1:\t%#zx\n"
			"SC2:\t%#zx\n"
			"SC3:\t%#zx\n"
			"SC4:\t%#zx\n"
			"UserStack:\t%#zx\n"
			"Path:\t%s\n"
			"VmSize:\t %ld kB\n"
			"RssShmem:\t %ld kB\n"
			"MemPermille:\t %ld\n"
			"LastCore:\t %d\n"
			"TotalTime:\t %ld ms\n"
			"SysTime:\t %ld ms\n"
			"CpuPermille:\t %d %d %d %d\n"
			"UserBrk:\t%#zx\n"
			,
			name,
			state,
			proc->group ? proc->group : proc->id,
			proc->id,
			parent ? parent->id : 0,
			proc->job,
			proc->session,
			proc->user,
			proc->syscall_registers ? arch_user_ip(proc->syscall_registers) : 0,
			proc->syscall_registers ? arch_syscall_number(proc->syscall_registers) : 0,
			proc->syscall_registers ? arch_syscall_arg0(proc->syscall_registers) : 0,
			proc->syscall_registers ? arch_syscall_arg1(proc->syscall_registers) : 0,
			proc->syscall_registers ? arch_syscall_arg2(proc->syscall_registers) : 0,
			proc->syscall_registers ? arch_syscall_arg3(proc->syscall_registers) : 0,
			proc->syscall_registers ? arch_syscall_arg4(proc->syscall_registers) : 0,
			proc->syscall_registers ? arch_stack_pointer(proc->syscall_registers) : 0,
			proc->cmdline ? proc->cmdline[0] : "(none)",
			mem_usage, shm_usage, mem_permille,
			proc->owner,
			proc->time_total / arch_cpu_mhz(),
			proc->time_sys / arch_cpu_mhz(),
			proc->usage[0], proc->usage[1], proc->usage[2], proc->usage[3],
			proc->image.heap
			);

	size_t _bsize = strlen(buf);
	if ((size_t)offset > _bsize) return 0;
	if (size > _bsize - offset) size = _bsize - offset;

	memcpy(buffer, buf + offset, size);
	return size;
}

static struct procfs_entry procdir_entries[] = {
	{1, "cmdline", proc_cmdline_func},
	{2, "status",  proc_status_func},
};

static struct dirent * readdir_procfs_procdir(fs_node_t *node, uint64_t index) {
	if (index == 0) {
		struct dirent * out = malloc(sizeof(struct dirent));
		memset(out, 0x00, sizeof(struct dirent));
		out->d_ino = 0;
		strcpy(out->d_name, ".");
		return out;
	}

	if (index == 1) {
		struct dirent * out = malloc(sizeof(struct dirent));
		memset(out, 0x00, sizeof(struct dirent));
		out->d_ino = 0;
		strcpy(out->d_name, "..");
		return out;
	}

	index -= 2;

	if (index < PROCFS_PROCDIR_ENTRIES) {
		struct dirent * out = malloc(sizeof(struct dirent));
		memset(out, 0x00, sizeof(struct dirent));
		out->d_ino = procdir_entries[index].id;
		strcpy(out->d_name, procdir_entries[index].name);
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
	snprintf(fnode->name, 100, "%d", pid);
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

static ssize_t cpuinfo_func(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
	char buf[4096];
	size_t _bsize = 0;

#ifdef __x86_64__
	for (int i = 0; i < processor_count; ++i) {
		_bsize += snprintf(buf + _bsize, 1000,
				"Processor: %d\n"
				"Manufacturer: %s\n"
				"MHz: %zd\n"
				"Family: %d\n"
				"Model: %d\n"
				"Model name: %s\n"
				"LAPIC id: %d\n"
				"\n",
				processor_local_data[i].cpu_id,
				processor_local_data[i].cpu_manufacturer,
				arch_cpu_mhz(), /* TODO Should this be per-cpu? */
				processor_local_data[i].cpu_family,
				processor_local_data[i].cpu_model,
				processor_local_data[i].cpu_model_name,
				processor_local_data[i].lapic_id
				);
	}
#elif defined(__aarch64__)
	for (int i = 0; i < processor_count; ++i) {
		_bsize += snprintf(buf + _bsize, 1000,
			"Processor: %d\n"
			"Implementer: %#x\n"
			"Variant: %#x\n"
			"Architecture: %#x\n"
			"PartNum: %#x\n"
			"Revision: %#x\n"
			"\n",
			processor_local_data[i].cpu_id,
			(unsigned int)(processor_local_data[i].midr >> 24) & 0xFF,
			(unsigned int)(processor_local_data[i].midr >> 20) & 0xF,
			(unsigned int)(processor_local_data[i].midr >> 16) & 0xF,
			(unsigned int)(processor_local_data[i].midr >> 4)  & 0xFFF,
			(unsigned int)(processor_local_data[i].midr >> 0)  & 0xF
			);
	}
#endif

	if ((size_t)offset > _bsize) return 0;
	if (size > _bsize - offset) size = _bsize - offset;

	memcpy(buffer, buf + offset, size);
	return size;
}

static ssize_t meminfo_func(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
	char buf[1024];
	size_t total = mmu_total_memory();
	size_t free  = total - mmu_used_memory();
	size_t kheap = ((uintptr_t)sbrk(0) - 0xffffff0000000000UL) / 1024;

	snprintf(buf, 1000,
		"MemTotal: %zu kB\n"
		"MemFree: %zu kB\n"
		"KHeapUse: %zu kB\n"
		, total, free, kheap);

	size_t _bsize = strlen(buf);
	if ((size_t)offset > _bsize) return 0;
	if (size > _bsize - offset) size = _bsize - offset;

	memcpy(buffer, buf + offset, size);
	return size;
}

#ifdef __x86_64__
static ssize_t pat_func(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
	char buf[1024];

	uint32_t pat_value_low, pat_value_high;
	asm volatile ( "rdmsr" : "=a" (pat_value_low), "=d" (pat_value_high): "c" (0x277) );
	uint64_t pat_values = ((uint64_t)pat_value_high << 32) | (pat_value_low);

	const char * pat_names[] = {
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

	snprintf(buf, 1000,
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
	if ((size_t)offset > _bsize) return 0;
	if (size > _bsize - offset) size = _bsize - offset;

	memcpy(buffer, buf + offset, size);
	return size;
}
#endif

static ssize_t uptime_func(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
	char buf[1024];
	unsigned long timer_ticks, timer_subticks;
	relative_time(0,0,&timer_ticks,&timer_subticks);
	snprintf(buf, 100, "%lu.%06lu\n", timer_ticks, timer_subticks);

	size_t _bsize = strlen(buf);
	if ((size_t)offset > _bsize) return 0;
	if (size > _bsize - offset) size = _bsize - offset;

	memcpy(buffer, buf + offset, size);
	return size;
}

static ssize_t cmdline_func(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
	char buf[1024];
	const char * cmdline = arch_get_cmdline();
	snprintf(buf, 1000, "%s\n", cmdline ? cmdline : "");

	size_t _bsize = strlen(buf);
	if ((size_t)offset > _bsize) return 0;
	if (size > _bsize - offset) size = _bsize - offset;

	memcpy(buffer, buf + offset, size);
	return size;
	return 0;
}

static ssize_t version_func(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
	char buf[1024];
	char version_number[512];
	snprintf(version_number, 510,
			__kernel_version_format,
			__kernel_version_major,
			__kernel_version_minor,
			__kernel_version_lower,
			__kernel_version_suffix);
	snprintf(buf, 1000, "%s %s %s %s %s %s\n",
			__kernel_name,
			version_number,
			__kernel_version_codename,
			__kernel_build_date,
			__kernel_build_time,
			__kernel_arch);

	size_t _bsize = strlen(buf);
	if ((size_t)offset > _bsize) return 0;
	if (size > _bsize - offset) size = _bsize - offset;

	memcpy(buffer, buf + offset, size);
	return size;
}

static ssize_t compiler_func(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
	char buf[1024];
	snprintf(buf, 1000, "%s\n", __kernel_compiler_version);

	size_t _bsize = strlen(buf);
	if ((size_t)offset > _bsize) return 0;
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
		c += snprintf(c, 5, "  ");
	}
	/* Get the current process */
	struct vfs_entry * fnode = (struct vfs_entry *)node->value;
	/* Print the process name */
	if (fnode->file) {
		c += snprintf(c, 100, "%s → %s %p (%s, %s)", fnode->name, fnode->device, (void*)fnode->file, fnode->fs_type, fnode->file->name);
	} else {
		c += snprintf(c, 100, "%s → (empty)", fnode->name);
	}
	/* Linefeed */
	snprintf(buf+strlen(buf), 100, "%s\n",tmp);
	free(tmp);
	foreach(child, node->children) {
		/* Recursively print the children */
		mount_recurse(buf+strlen(buf),child->value, height + 1);
	}
}

static ssize_t mounts_func(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
	char * buf = malloc(4096);

	buf[0] = '\0';

	mount_recurse(buf, fs_tree->root, 0);

	size_t _bsize = strlen(buf);
	if ((size_t)offset > _bsize) {
		free(buf);
		return 0;
	}
	if (size > _bsize - offset) size = _bsize - offset;

	memcpy(buffer, buf + offset, size);
	free(buf);
	return size;
}

static ssize_t modules_func(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
	list_t * hash_keys = hashmap_keys(modules_get_list());
	if (!hash_keys || !hash_keys->length) return 0;
	char * buf = malloc(hash_keys->length * 512);
	unsigned int soffset = 0;
	foreach(_key, hash_keys) {
		char * key = (char *)_key->value;
		struct LoadedModule * mod_info = hashmap_get(modules_get_list(), key);
		soffset += snprintf(&buf[soffset], 100, "%#zx %zu %zu %s\n",
			mod_info->baseAddress,
			mod_info->fileSize,
			mod_info->loadedSize,
			key);
	}
	free(hash_keys);

	size_t _bsize = strlen(buf);
	if ((size_t)offset > _bsize) {
		free(buf);
		return 0;
	}
	if (size > _bsize - offset) size = _bsize - offset;

	memcpy(buffer, buf + offset, size);
	free(buf);
	return size;
}

extern hashmap_t * fs_types; /* from kernel/fs/vfs.c */

static ssize_t filesystems_func(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
	list_t * hash_keys = hashmap_keys(fs_types);
	if (!hash_keys || !hash_keys->length) return 0;
	char * buf = malloc(hash_keys->length * 512);
	unsigned int soffset = 0;
	foreach(_key, hash_keys) {
		char * key = (char *)_key->value;
		soffset += snprintf(&buf[soffset], 100, "%s\n", key);
	}
	free(hash_keys);

	size_t _bsize = strlen(buf);
	if ((size_t)offset > _bsize) {
		free(buf);
		return 0;
	}
	if (size > _bsize - offset) size = _bsize - offset;

	memcpy(buffer, buf + offset, size);
	free(buf);
	return size;
}

static ssize_t loader_func(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
	char * buf = malloc(512);

	snprintf(buf, 511, "%s\n", arch_get_loader());

	size_t _bsize = strlen(buf);
	if ((size_t)offset > _bsize) {
		free(buf);
		return 0;
	}
	if (size > _bsize - offset) size = _bsize - offset;

	memcpy(buffer, buf + offset, size);
	free(buf);
	return size;
}

#ifdef __x86_64__
#include <kernel/arch/x86_64/irq.h>
#include <kernel/arch/x86_64/ports.h>
static ssize_t irq_func(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
	char * buf = malloc(4096);
	unsigned int soffset = 0;

	for (int i = 0; i < 16; ++i) {
		soffset += snprintf(&buf[soffset], 100, "irq %d: ", i);
		for (int j = 0; j < 4; ++j) {
			const char * t = get_irq_handler(i, j);
			if (!t) break;
			soffset += snprintf(&buf[soffset], 100, "%s%s", j ? "," : "", t);
		}
		soffset += snprintf(&buf[soffset], 100, "\n");
	}

	outportb(0x20, 0x0b);
	outportb(0xa0, 0x0b);
	soffset += snprintf(&buf[soffset], 100, "isr=0x%04x\n", (inportb(0xA0) << 8) | inportb(0x20));

	outportb(0x20, 0x0a);
	outportb(0xa0, 0x0a);
	soffset += snprintf(&buf[soffset], 100, "irr=0x%04x\n", (inportb(0xA0) << 8) | inportb(0x20));

	soffset += snprintf(&buf[soffset], 100, "imr=0x%04x\n", (inportb(0xA1) << 8) | inportb(0x21));

	size_t _bsize = strlen(buf);
	if ((size_t)offset > _bsize) {
		free(buf);
		return 0;
	}
	if (size > _bsize - offset) size = _bsize - offset;

	memcpy(buffer, buf + offset, size);
	free(buf);
	return size;
}
#endif

/**
 * Basically the same as the kdebug `pci` command.
 */
struct _pci_buf {
	size_t   offset;
	char *buffer;
};

static void scan_hit_list(uint32_t device, uint16_t vendorid, uint16_t deviceid, void * extra) {

	struct _pci_buf * b = extra;

	b->offset += snprintf(b->buffer + b->offset, 100, "%02x:%02x.%d (%04x, %04x:%04x)\n",
			(int)pci_extract_bus(device),
			(int)pci_extract_slot(device),
			(int)pci_extract_func(device),
			(int)pci_find_type(device),
			vendorid,
			deviceid);

	b->offset += snprintf(b->buffer + b->offset, 100, " BAR0: 0x%08x", pci_read_field(device, PCI_BAR0, 4));
	b->offset += snprintf(b->buffer + b->offset, 100, " BAR1: 0x%08x", pci_read_field(device, PCI_BAR1, 4));
	b->offset += snprintf(b->buffer + b->offset, 100, " BAR2: 0x%08x", pci_read_field(device, PCI_BAR2, 4));
	b->offset += snprintf(b->buffer + b->offset, 100, " BAR3: 0x%08x", pci_read_field(device, PCI_BAR3, 4));
	b->offset += snprintf(b->buffer + b->offset, 100, " BAR4: 0x%08x", pci_read_field(device, PCI_BAR4, 4));
	b->offset += snprintf(b->buffer + b->offset, 100, " BAR5: 0x%08x\n", pci_read_field(device, PCI_BAR5, 4));

	b->offset += snprintf(b->buffer + b->offset, 100, " IRQ Line: %d", pci_read_field(device, 0x3C, 1));
	b->offset += snprintf(b->buffer + b->offset, 100, " IRQ Pin: %d", pci_read_field(device, 0x3D, 1));
	b->offset += snprintf(b->buffer + b->offset, 100, " Interrupt: %d", pci_get_interrupt(device));
	b->offset += snprintf(b->buffer + b->offset, 100, " Status: 0x%04x\n", pci_read_field(device, PCI_STATUS, 2));
}

static void scan_count(uint32_t device, uint16_t vendorid, uint16_t deviceid, void * extra) {
	size_t * count = extra;
	(*count)++;
}

static ssize_t pci_func(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
	size_t count = 0;
	pci_scan(&scan_count, -1, &count);

	struct _pci_buf b = {0,NULL};
	b.buffer = malloc(count * 1024);

	pci_scan(&scan_hit_list, -1, &b);

	size_t _bsize = b.offset;
	if ((size_t)offset > _bsize) {
		free(b.buffer);
		return 0;
	}
	if (size > _bsize - offset) size = _bsize - offset;

	memcpy(buffer, b.buffer + offset, size);
	free(b.buffer);
	return size;
}

static ssize_t idle_func(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
	char * buf = malloc(4096);
	unsigned int soffset = 0;

	for (int i = 0; i < processor_count; ++i) {
		soffset += snprintf(&buf[soffset], 100, "%d: %4d %4d %4d %4d\n",
			i,
			processor_local_data[i].kernel_idle_task->usage[0],
			processor_local_data[i].kernel_idle_task->usage[1],
			processor_local_data[i].kernel_idle_task->usage[2],
			processor_local_data[i].kernel_idle_task->usage[3]
		);
	}

	size_t _bsize = strlen(buf);
	if ((size_t)offset > _bsize) {
		free(buf);
		return 0;
	}
	if (size > _bsize - offset) size = _bsize - offset;

	memcpy(buffer, buf + offset, size);
	free(buf);
	return size;
}

static ssize_t kallsyms_func(fs_node_t *node, off_t offset, size_t size, uint8_t * buffer) {
	/* This doesn't include module symbols at the moment... */
	list_t * syms = ksym_list();

	/* Figure out how much space needs to go in this buffer */
	size_t total_size = 0;
	foreach (node, syms) {
		/* 16 for the address, one space, len(sym), one linefeed = 18 + strlen() */
		total_size += 18 + strlen((char*)node->value);
	}

	char * buf = malloc(total_size);

	size_t soffset = 0;
	foreach(node, syms) {
		soffset += snprintf(&buf[soffset], 100, "%016zx %s\n", this_core->current_process->user == USER_ROOT_UID ? (uintptr_t)ksym_lookup(node->value) : (uintptr_t)0, (char*)node->value);
	}

	if ((size_t)offset >= soffset) {
		size = 0;
		goto free_results;
	}

	if (size > soffset - offset) size = soffset - offset;
	memcpy(buffer, buf + offset, size);

free_results:
	list_free(syms);
	free(syms);
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
	{-8, "modules",  modules_func},
	{-9, "filesystems", filesystems_func},
	{-10,"loader",   loader_func},
	{-11,"idle",     idle_func},
	{-12,"kallsyms", kallsyms_func},
	{-13,"pci",      pci_func},
#ifdef __x86_64__
	{-14,"irq",      irq_func},
	{-15,"pat",      pat_func},
#endif
};

static list_t * extended_entries = NULL;
static long next_id = 0;

int procfs_install(struct procfs_entry * entry) {
	if (!extended_entries) {
		extended_entries = list_create("procfs entries",NULL);
		next_id = -PROCFS_STANDARD_ENTRIES - 1;
	}

	entry->id = next_id--;
	list_insert(extended_entries, entry);

	return 0;
}

static struct dirent * readdir_procfs_root(fs_node_t *node, uint64_t index) {
	if (index == 0) {
		struct dirent * out = malloc(sizeof(struct dirent));
		memset(out, 0x00, sizeof(struct dirent));
		out->d_ino = 0;
		strcpy(out->d_name, ".");
		return out;
	}

	if (index == 1) {
		struct dirent * out = malloc(sizeof(struct dirent));
		memset(out, 0x00, sizeof(struct dirent));
		out->d_ino = 0;
		strcpy(out->d_name, "..");
		return out;
	}

	if (index == 2) {
		struct dirent * out = malloc(sizeof(struct dirent));
		memset(out, 0x00, sizeof(struct dirent));
		out->d_ino = 0;
		strcpy(out->d_name, "self");
		return out;
	}

	index -= 3;

	if (index < PROCFS_STANDARD_ENTRIES) {
		struct dirent * out = malloc(sizeof(struct dirent));
		memset(out, 0x00, sizeof(struct dirent));
		out->d_ino = std_entries[index].id;
		strcpy(out->d_name, std_entries[index].name);
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
			out->d_ino = e->id;
			strcpy(out->d_name, e->name);
			return out;
		}
		index -=  extended_entries->length;
	}

	int i = index + 1;

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
	out->d_ino  = pid;
	snprintf(out->d_name, 100, "%d", pid);

	return out;
}

static ssize_t readlink_self(fs_node_t * node, char * buf, size_t size) {
	char tmp[30];
	size_t req;
	snprintf(tmp, 100, "/proc/%d", this_core->current_process->id);
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

	if (extended_entries) {
		foreach(node, extended_entries) {
			struct procfs_entry * e = node->value;
			if (!strcmp(name, e->name)) {
				fs_node_t * out = procfs_generic_create(e->name, e->func);
				return out;
			}
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

void procfs_initialize(void) {
	/* TODO Move this to some sort of config */
	vfs_mount("/proc", procfs_create());

	//debug_print_vfs_tree();
}

