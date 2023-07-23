/**
 * @file  kernel/vfs/procfs.c
 * @brief Extensible file-based information interface.
 *
 * Provides /proc and its contents, which allow userspace tools
 * to query kernel status through directory and text file interfaces.
 *
 * When a procfs entry is opened, a dynamic buffer is allocated and
 * the bound function is called. The function can then print into
 * the buffer, which will expand as necessary. Reads on the device
 * will then return data from that buffer. When the file node for
 * the entry is later closed, the dynamic buffer is freed. This
 * resolves a long-standing issue with the previous implementation
 * where subsequent reads could return corrupted data if offsets
 * changed from newly generated data.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2014-2023 K. Lange
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

typedef struct procfs_entry_node {
	fs_node_t fnode;
	char * buf;
	size_t avail;
	size_t used;
	procfs_populate_t func;
} procfs_entry_t;

static ssize_t procfs_entry_read(fs_node_t * node, off_t offset, size_t size, uint8_t *buffer) {
	procfs_entry_t * entry = (void*)node;
	if ((size_t)offset > entry->used) return 0;
	if (size > entry->used - offset) size = entry->used - offset;
	memcpy(buffer, (uint8_t*)entry->buf + offset, size);
	return size;
}


/**
 * Dynamic reallocating printf thingy
 */

static int procfs_cb(void * user, char c) {
	procfs_entry_t * entry = user;

	if (entry->used >= entry->avail) {
		entry->avail += 64;
		entry->buf = realloc(entry->buf, entry->avail);
	}

	entry->buf[entry->used] = c;
	entry->used++;
	return 0;
}

int procfs_printf(fs_node_t * node, const char * fmt, ...) {
	va_list args;
	va_start(args, fmt);
	int out = xvasprintf(procfs_cb, node, fmt, args);
	va_end(args);
	return out;
}


static void procfs_entry_open(fs_node_t * node, unsigned int flags) {
	procfs_entry_t * entry = (void*)node;
	entry->func(node);
}

static void procfs_entry_close(fs_node_t * node) {
	procfs_entry_t * entry = (void*)node;
	if (entry->avail) free(entry->buf);
	entry->buf = NULL;
	entry->avail = 0;
}

static fs_node_t * procfs_generic_create(const char * name, procfs_populate_t read_func) {
	procfs_entry_t * entry = malloc(sizeof(procfs_entry_t));
	memset(entry, 0x00, sizeof(procfs_entry_t));
	entry->fnode.inode = 0;
	strcpy(entry->fnode.name, name);

	entry->buf = NULL;
	entry->avail = 0;
	entry->used = 0;
	entry->func = read_func;

	entry->fnode.uid = 0;
	entry->fnode.gid = 0;
	entry->fnode.mask    = 0444;
	entry->fnode.flags   = FS_FILE;
	entry->fnode.read    = procfs_entry_read;
	entry->fnode.write   = NULL;
	entry->fnode.open    = procfs_entry_open;
	entry->fnode.close   = procfs_entry_close;
	entry->fnode.readdir = NULL;
	entry->fnode.finddir = NULL;
	entry->fnode.ctime   = now();
	entry->fnode.mtime   = now();
	entry->fnode.atime   = now();
	return &entry->fnode;
}

static void proc_cmdline_func(fs_node_t *node) {
	process_t * proc = process_from_pid(node->inode);

	if (!proc) {
		/* wat */
		return;
	}

	if (!proc->cmdline) {
		procfs_printf(node, "%s", proc->name);
		return;
	}

	char ** args = proc->cmdline;
	while (*args) {
		procfs_printf(node, "%s", *args);
		if (*(args+1)) {
			procfs_printf(node, "\036");
		}
		args++;
	}
}

static void proc_status_func(fs_node_t *node) {
	process_t * proc = process_from_pid(node->inode);
	process_t * parent = process_get_parent(proc);

	if (!proc) {
		/* wat */
		return;
	}

	char state = 'S';

	/* Base state */
	if ((proc->flags & PROC_FLAG_RUNNING) || process_is_ready(proc)) {
		state = 'R'; /* Running or runnable */
	} else if ((proc->flags & PROC_FLAG_FINISHED)) {
		state = 'Z'; /* Zombie - exited but not yet reaped */
	} else if ((proc->flags & PROC_FLAG_SUSPENDED)) {
		state = 'T'; /* Stopped; TODO can we differentiate stopped tracees correctly? */
	}

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

	procfs_printf(node,
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
			"TotalTime:\t %ld us\n"
			"SysTime:\t %ld us\n"
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

static void cpuinfo_func(fs_node_t *node) {
#ifdef __x86_64__
	for (int i = 0; i < processor_count; ++i) {
		procfs_printf(node,
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
		procfs_printf(node,
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
}

static void meminfo_func(fs_node_t *node) {
	size_t total = mmu_total_memory();
	size_t free  = total - mmu_used_memory();
	size_t kheap = ((uintptr_t)sbrk(0) - 0xffffff0000000000UL) / 1024;

	procfs_printf(node,
		"MemTotal: %zu kB\n"
		"MemFree: %zu kB\n"
		"KHeapUse: %zu kB\n"
		, total, free, kheap);
}

#ifdef __x86_64__
static void pat_func(fs_node_t *node) {
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

	procfs_printf(node,
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
}
#endif

static void uptime_func(fs_node_t *node) {
	unsigned long timer_ticks, timer_subticks;
	relative_time(0,0,&timer_ticks,&timer_subticks);
	procfs_printf(node, "%lu.%06lu\n", timer_ticks, timer_subticks);
}

static void cmdline_func(fs_node_t *node) {
	const char * cmdline = arch_get_cmdline();
	procfs_printf(node, "%s\n", cmdline ? cmdline : "");
}

static void version_func(fs_node_t *node) {
	procfs_printf(node, "%s ", __kernel_name);
	procfs_printf(node, __kernel_version_format,
			__kernel_version_major,
			__kernel_version_minor,
			__kernel_version_lower,
			__kernel_version_suffix);
	procfs_printf(node, " %s %s %s %s\n",
			__kernel_version_codename,
			__kernel_build_date,
			__kernel_build_time,
			__kernel_arch);
}

static void compiler_func(fs_node_t *node) {
	procfs_printf(node, "%s\n", __kernel_compiler_version);
}

extern tree_t * fs_tree; /* kernel/fs/vfs.c */

static void mount_recurse(fs_node_t * pnode, tree_node_t * node, size_t height) {
	/* End recursion on a blank entry */
	if (!node) return;
	/* Indent output */
	for (uint32_t i = 0; i < height; ++i) {
		procfs_printf(pnode, "  ");
	}
	/* Get the current process */
	struct vfs_entry * fnode = (struct vfs_entry *)node->value;
	/* Print the process name */
	if (fnode->file) {
		procfs_printf(pnode, "%s → %s %p (%s, %s)\n", fnode->name, fnode->device, (void*)fnode->file, fnode->fs_type, fnode->file->name);
	} else {
		procfs_printf(pnode, "%s → (empty)\n", fnode->name);
	}
	/* Linefeed */
	foreach(child, node->children) {
		/* Recursively print the children */
		mount_recurse(pnode, child->value, height + 1);
	}
}

static void mounts_func(fs_node_t *node) {
	mount_recurse(node, fs_tree->root, 0);
}

static void modules_func(fs_node_t *node) {
	list_t * hash_keys = hashmap_keys(modules_get_list());
	if (!hash_keys || !hash_keys->length) return;
	foreach(_key, hash_keys) {
		char * key = (char *)_key->value;
		struct LoadedModule * mod_info = hashmap_get(modules_get_list(), key);
		procfs_printf(node, "%#zx %zu %zu %s\n",
			mod_info->baseAddress,
			mod_info->fileSize,
			mod_info->loadedSize,
			key);
	}
	free(hash_keys);
}

extern hashmap_t * fs_types; /* from kernel/fs/vfs.c */

static void filesystems_func(fs_node_t *node) {
	list_t * hash_keys = hashmap_keys(fs_types);
	if (!hash_keys || !hash_keys->length) return;
	foreach(_key, hash_keys) {
		char * key = (char *)_key->value;
		procfs_printf(node, "%s\n", key);
	}
	free(hash_keys);
}

static void loader_func(fs_node_t *node) {
	procfs_printf(node, "%s\n", arch_get_loader());
}

#ifdef __x86_64__
#include <kernel/arch/x86_64/irq.h>
#include <kernel/arch/x86_64/ports.h>
static void irq_func(fs_node_t *node) {
	for (int i = 0; i < 16; ++i) {
		procfs_printf(node, "irq %d: ", i);
		for (int j = 0; j < 4; ++j) {
			const char * t = get_irq_handler(i, j);
			if (!t) break;
			procfs_printf(node, "%s%s", j ? "," : "", t);
		}
		procfs_printf(node, "\n");
	}

	outportb(0x20, 0x0b);
	outportb(0xa0, 0x0b);
	procfs_printf(node, "isr=0x%04x\n", (inportb(0xA0) << 8) | inportb(0x20));

	outportb(0x20, 0x0a);
	outportb(0xa0, 0x0a);
	procfs_printf(node, "irr=0x%04x\n", (inportb(0xA0) << 8) | inportb(0x20));

	procfs_printf(node, "imr=0x%04x\n", (inportb(0xA1) << 8) | inportb(0x21));
}
#endif

/**
 * Basically the same as the kdebug `pci` command.
 */
static void scan_hit_list(uint32_t device, uint16_t vendorid, uint16_t deviceid, void * extra) {
	fs_node_t * node = extra;

	procfs_printf(node, "%02x:%02x.%d (%04x, %04x:%04x)\n",
			(int)pci_extract_bus(device),
			(int)pci_extract_slot(device),
			(int)pci_extract_func(device),
			(int)pci_find_type(device),
			vendorid,
			deviceid);

	procfs_printf(node, " BAR0: 0x%08x", pci_read_field(device, PCI_BAR0, 4));
	procfs_printf(node, " BAR1: 0x%08x", pci_read_field(device, PCI_BAR1, 4));
	procfs_printf(node, " BAR2: 0x%08x", pci_read_field(device, PCI_BAR2, 4));
	procfs_printf(node, " BAR3: 0x%08x", pci_read_field(device, PCI_BAR3, 4));
	procfs_printf(node, " BAR4: 0x%08x", pci_read_field(device, PCI_BAR4, 4));
	procfs_printf(node, " BAR5: 0x%08x\n", pci_read_field(device, PCI_BAR5, 4));

	procfs_printf(node, " IRQ Line: %d", pci_read_field(device, 0x3C, 1));
	procfs_printf(node, " IRQ Pin: %d", pci_read_field(device, 0x3D, 1));
	procfs_printf(node, " Interrupt: %d", pci_get_interrupt(device));
	procfs_printf(node, " Status: 0x%04x\n", pci_read_field(device, PCI_STATUS, 2));
}

static void pci_func(fs_node_t *node) {
	pci_scan(&scan_hit_list, -1, node);
}

static void idle_func(fs_node_t *node) {
	for (int i = 0; i < processor_count; ++i) {
		procfs_printf(node, "%d: %4d %4d %4d %4d\n",
			i,
			processor_local_data[i].kernel_idle_task->usage[0],
			processor_local_data[i].kernel_idle_task->usage[1],
			processor_local_data[i].kernel_idle_task->usage[2],
			processor_local_data[i].kernel_idle_task->usage[3]
		);
	}
}

static void kallsyms_func(fs_node_t *fnode) {
	/* This doesn't include module symbols at the moment... */
	list_t * syms = ksym_list();

	foreach(node, syms) {
		procfs_printf(fnode, "%016zx %s\n", this_core->current_process->user == USER_ROOT_UID ? (uintptr_t)ksym_lookup(node->value) : (uintptr_t)0, (char*)node->value);
	}

	list_free(syms);
	free(syms);
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

