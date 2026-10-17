/**
 * @file   kernel/sys/process.c
 * @brief  Task switch and thread scheduling.
 *
 * Implements the primary scheduling primitives for the kernel.
 *
 * Generally, what the kernel refers to as a "process" is an individual thread.
 * The POSIX concept of a "process" is represented in Misaka as a collection of
 * threads and their shared paging, signal, and file descriptor tables.
 *
 * Kernel threads are also "processes", referred to as "tasklets".
 *
 * Misaka allows nested kernel preemption, and task switching involves saving
 * kernel state in a manner similar to setjmp/longjmp, as well as saving the
 * outer context in the case of a nested task switch.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2011-2021 K. Lange
 * Copyright (C) 2012 Markus Schober
 * Copyright (C) 2015 Dale Weiler
 */
#include <bits/errno.h>
#include <kernel/assert.h>
#include <kernel/process.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/vfs.h>
#include <kernel/spinlock.h>
#include <kernel/list.h>
#include <kernel/mmu.h>
#include <kernel/signal.h>
#include <kernel/time.h>
#include <kernel/misc.h>
#include <kernel/syscall.h>
#include <kernel/ksym.h>
#include <kernel/pty.h>
#include <kernel/ptrace.h>
#include <kernel/args.h>
#include <kernel/waitable.h>
#include <sys/wait.h>
#include <sys/signal_defs.h>
#include <sys/uregs.h>
#include <bits/poll.h>

struct timer_sleep {
	struct elist el;
	uint64_t end_tick;
	uint64_t end_subtick;
	int (*callback)(struct timer_sleep *, void *);
	void * priv;
	void * key;
};

static struct elist sleep_list = {0};

process_t * process_tree;  /* Stores the parent-child process relationships; the root of this graph is 'init'. */
list_t * process_list;  /* Stores all existing processes. Mostly used for sanity checking or for places where iterating over all processes is useful. */
list_t * reap_queue;    /* Processes that could not be cleaned up and need to be deleted. */

struct ProcessorLocal processor_local_data[32] = {0};
int processor_count = 1;

/* The following locks protect access to the process tree, scheduler queue,
 * sleeping, and the very special wait queue... */
static spin_lock_t tree_lock = { 0 };
static spin_lock_t process_queue_lock = { 0 };
static spin_lock_t wait_lock_tmp = { 0 };
static spin_lock_t sleep_lock = { 0 };
static spin_lock_t reap_lock = { 0 };

/**
 * Update both the total time and the system time when switching to a new thread
 * or exiting the current thread.
 */
void update_process_times(void) {
	uint64_t pTime = arch_perf_timer();
	if (this_core->current_process->time_in && this_core->current_process->time_in < pTime) {
		this_core->current_process->time_total +=  pTime - this_core->current_process->time_in;
		this_core->time_total += pTime - this_core->current_process->time_in;
	}
	this_core->current_process->time_in = 0;

	if (this_core->current_process->time_switch && this_core->current_process->time_switch < pTime) {
		this_core->current_process->time_sys += pTime - this_core->current_process->time_switch;
		this_core->time_sys += pTime - this_core->current_process->time_switch;
	}
	this_core->current_process->time_switch = 0;
}

/**
 * Add time spent in kernel to time_sys when returning to userspace, such as through
 * an interrupt return or entry into a signal handler.
 */
void update_process_times_on_exit(void) {
	uint64_t pTime = arch_perf_timer();
	if (this_core->current_process->time_switch && this_core->current_process->time_switch < pTime) {
		this_core->current_process->time_sys += pTime - this_core->current_process->time_switch;
		this_core->time_sys += pTime - this_core->current_process->time_switch;
	}
	this_core->current_process->time_switch = 0;
}

#define must_have_lock(lck) if (lck.owner != this_core->cpu_id+1) { arch_fatal_prepare(); printf("Failed lock check.\n"); arch_dump_traceback(); arch_fatal(); }

/**
 * @brief Restore the context of the next available process's kernel thread.
 *
 * Loads the next ready process from the scheduler queue and resumes it.
 *
 * If no processes are available, the local idle task will be run from the beginning
 * of its function entry.
 *
 * If the next process in the queue has been marked as finished, it will be discard
 * until a non-finished process is found.
 *
 * If the next process is new, it will be marked as started, and its entry point
 * jumped to.
 *
 * For all other cases, the process's stored kernel thread state will be restored
 * and execution will contain in @ref switch_task with a return value of 1.
 *
 * Note that switch_next does not return and should be called only when the current
 * process has been properly added to a scheduling queue, or marked as awaiting cleanup,
 * otherwise its return state if resumed is undefined and generally whatever the state
 * was when that process last entered switch_task.
 *
 * @returns never.
 */
void switch_next(void) {
	this_core->previous_process = this_core->current_process;
	update_process_times();

	/* Get the next available process, discarded anything in the queue
	 * marked as finished. */
	do {
		this_core->current_process = next_ready_process();
	} while (this_core->current_process->flags & PROC_FLAG_FINISHED);

	this_core->current_process->time_in = arch_perf_timer();
	this_core->current_process->time_switch = this_core->current_process->time_in;

	/* Restore paging and task switch context. */
	mmu_set_directory(this_core->current_process->thread.page_directory->directory);
	arch_set_kernel_stack(this_core->current_process->image.stack);

	if (this_core->current_process->flags & PROC_FLAG_FINISHED) {
		arch_fatal_prepare();
		printf("Should not have this process...\n");
		if (this_core->current_process->flags & PROC_FLAG_FINISHED) printf("It is marked finished.\n");
		arch_dump_traceback();
		arch_fatal();
		__builtin_unreachable();
	}

	/* Mark the process as running and started. */
	__sync_or_and_fetch(&this_core->current_process->flags, PROC_FLAG_STARTED);

	asm volatile ("" ::: "memory");

	/* Jump to next */
	arch_restore_context(&this_core->current_process->thread);
	__builtin_unreachable();
}

/**
 * @brief Yield the processor to the next available task.
 *
 * Yields the current process, allowing the next to run. Can be called both as
 * part of general preemption or from blocking tasks; in the latter case,
 * the process should be added to a scheduler queue to be awakoen later when the
 * blocking operation is completed and @p reschedule should be set to 0.
 *
 * @param reschedule Non-zero if this process should be added to the ready queue.
 */
void switch_task(uint8_t reschedule) {

	/* switch_task() called but the scheduler isn't enabled? Resume... this is probably a bug. */
	if (!this_core->current_process) return;

	if (this_core->current_process == this_core->kernel_idle_task) {
		arch_fatal_prepare();
		printf("Context switch from kernel_idle_task triggered from somewhere other than pre-emption source. Halting.\n");
		printf("This generally means that a driver responding to interrupts has attempted to yield in its interrupt context.\n");
		printf("Ensure that all device drivers which respond to interrupts do so with non-blocking data structures.\n");
		printf("   Return address of switch_task: %p\n", __builtin_return_address(0));
		arch_dump_traceback();
		arch_fatal();
	}

	/* If a process got to switch_task but was not marked as running, it must be exiting and we don't
	 * want to waste time saving context for it. Also, kidle is always resumed from the top of its
	 * loop function, so we don't save any context for it either. */
	if (!(this_core->current_process->flags & PROC_FLAG_RUNNING) || (this_core->current_process == this_core->kernel_idle_task)) {
		switch_next();
		return;
	}

	arch_save_floating((process_t*)this_core->current_process);

	/* 'setjmp' - save the execution context. When this call returns '1' we are back
	 * from a task switch and have been awoken if we were sleeping. */
	if (arch_save_context(&this_core->current_process->thread) == 1) {
		arch_restore_floating((process_t*)this_core->current_process);
		return;
	}

	/* If this is a normal yield, we reschedule.
	 * XXX: Is this going to work okay with SMP? I think this whole thing
	 *      needs to be wrapped in a lock, but also what if we put the
	 *      thread into a schedule queue previously but a different core
	 *      picks it up before we saved the thread context or the FPU state... */
	if (reschedule) {
		make_process_ready((process_t*)this_core->current_process);
	}

	/* @ref switch_next() does not return. */
	switch_next();
}

static process_t dummy_process = {0};

/**
 * @brief Initial scheduler datastructures.
 *
 * Called by early system startup to allocate trees and lists
 * the schedule uses to track processes.
 */
void initialize_process_tree(void) {
	for (size_t i = 0; i < sizeof(processor_local_data)/sizeof(*processor_local_data); ++i) {
		elist_init(&processor_local_data[i].readyqueue);
		processor_local_data[i].readyqueue_p = &processor_local_data[i].readyqueue;
	}
	elist_init(&sleep_list);

	process_list = list_create("global process list",NULL);
	reap_queue = list_create("processes awaiting later cleanup",NULL);

	this_core->current_process = &dummy_process;

	/* TODO: PID bitset? */
}

/**
 * @brief Determines if a process is alive and valid.
 *
 * Scans @ref process_list to see if @p process is a valid
 * process object or not.
 *
 * XXX This is horribly inefficient, and its very existence
 *     is likely indicative of bugs whereever it needed to
 *     be called...
 *
 * @param process Process object to check.
 * @returns 1 if the process is valid, 0 if it is not.
 */
int is_valid_process(process_t * process) {
	foreach(lnode, process_list) {
		if (lnode->value == process) {
			return 1;
		}
	}

	return 0;
}

/**
 * @brief Grow the FD table for a process by doubling its capacity.
 */
static void process_fds_grow(process_t * proc) {
	proc->fds->capacity *= 2;
	proc->fds->entries = realloc(proc->fds->entries, sizeof(uintptr_t) * proc->fds->capacity);
}

/**
 * @brief Duplicate a file descriptor to a new table entry.
 */
static void process_fds_copy(process_t * proc, long src, long dest, int extra_mode) {
	proc->fds->entries[dest] = fs_clone_desc(FD_PTR_MASK(proc->fds->entries[src]), extra_mode);
}

/**
 * @brief Allocate a new file descriptor.
 *
 * Adds a new file descriptor for the given file description and
 * close modes (FD_PTR_ flags) to a process's file descriptor table.
 *
 * @param proc Process whose file descriptor table should be modified.
 * @param desc File description, reference will be taken.
 * @param mode FD_PTR flags for close mode.
 * @returns the new file descriptor index
 */
unsigned long process_append_fd(process_t * proc, struct fs_file_description * desc, uintptr_t mode) {
	spin_lock(proc->fds->lock);
	/* Fill gaps */
	for (unsigned long i = 0; i < proc->fds->length; ++i) {
		if (!proc->fds->entries[i]) {
			proc->fds->entries[i] = ((uintptr_t)desc | mode);
			spin_unlock(proc->fds->lock);
			return i;
		}
	}
	/* No gaps, expand */
	if (proc->fds->length == proc->fds->capacity) {
		process_fds_grow(proc);
	}
	proc->fds->entries[proc->fds->length] = ((uintptr_t)desc | mode);
	unsigned long out = proc->fds->length++;
	spin_unlock(proc->fds->lock);
	return out;
}

/**
 * @brief Allocate a file descriptor from an anonymous fs_node, such as a socket.
 *
 * Allocates a new open file description for the given fs_node, with the requested
 * permissions from @p flags (PROC_FD_MODE_*) and the given path object (which can
 * be allocated via, eg., @c fs_path_printf or @c fs_alloc_path_from for such an
 * anonymous node).
 *
 * @param proc Process whose file descriptor table should be modified.
 * @param node File node to attach to the new description and descriptor.
 * @param flags PROC_FD_MODE_* flags for the new description and descriptor.
 * @returns The new file descriptor index
 */
unsigned long process_new_fd(process_t * proc, fs_node_t * node, int flags, struct fs_path * path) {
	struct fs_file_description * desc =  fs_fresh_descriptor(node, flags, path);

	uintptr_t fd_flags = 0;
	if (flags & PROC_FD_MODE_CLOEXEC) fd_flags |= FD_PTR_CLOEXEC;
	if (flags & PROC_FD_MODE_CLOFORK) fd_flags |= FD_PTR_CLOFORK;

	return process_append_fd(proc, desc, fd_flags);
}

/**
 * @brief Change the working directory of a process.
 *
 * Assigns a new working directory to a process and closes the previous one.
 * A new reference to the provided file description will be made.
 *
 * @param proc Process to modify the working directory for.
 * @param newfd New working directory file description.
 */
void process_chdir(process_t * proc, struct fs_file_description * newfd) {
	struct fs_file_description *old = proc->wd;
	proc->wd = (void*)fs_clone_desc(newfd, 0);
	if (old) fs_close_desc(old);
	if (proc->wd->inode == NULL) {
		arch_fatal_prepare();
		dprintf("chdir into NULL\n");
		arch_dump_traceback();
		arch_fatal();
	}
}

/**
 * @brief Duplicate a file descriptor to a minimum value.
 *
 * Duplicate the file descriptor at @c oldfd to a new file descriptor
 * with a number of a least @c newfd - always allocating a new
 * file descriptor in the process.
 *
 * If the current table can not hold @c newfd then the table is
 * expanded and any unused entries are left empty and will
 * be filled by @c process_append_fd normally.
 *
 * @param proc Process to modify.
 * @param oldfd File descriptor to copy from.
 * @param newfd Minimum value of new file descriptor.
 * @returns The actual newly allocated file descriptor.
 */
long process_fd_dup_least(process_t * proc, long oldfd, long newfd, int flags) {
	spin_lock(proc->fds->lock);

	/* Ensure there is at least enough space for for newfd itself */
	while (proc->fds->capacity <= (unsigned long)newfd) {
		process_fds_grow(proc);
	}

	/* Then ensure length is at least newfds */
	while (proc->fds->length <= (unsigned long)newfd) {
		proc->fds->entries[proc->fds->length] = 0;
		proc->fds->length++;
	}

	/* Then check if anything is available already */
	for (size_t i = newfd; i < proc->fds->length; ++i) {
		if (proc->fds->entries[i] == 0) {
			/* Found a hit, copy */
			process_fds_copy(proc, oldfd, i, flags);
			spin_unlock(proc->fds->lock);
			return i;
		}
	}

	/* Otherwise we need to keep expanding */
	if (proc->fds->length == proc->fds->capacity) {
		process_fds_grow(proc);
	}

	long i = proc->fds->length;
	process_fds_copy(proc, oldfd, i, flags);
	proc->fds->length++;
	spin_unlock(proc->fds->lock);

	return i;
}

/**
 * @brief Allocate a process identifier.
 *
 * Obtains the next available process identifier.
 *
 * FIXME This used to use a bitset in Toaru32 so it could
 *       handle overflow of the pid counter. We need to
 *       bring that back.
 */
pid_t get_next_pid(void) {
	static pid_t _next_pid = 2;
	return __sync_fetch_and_add(&_next_pid,1);
}

/**
 * @brief The idle task.
 *
 * Sits in a loop forever. Scheduled whenever there is nothing
 * else to do. Actually always enters from the top of the function
 * whenever scheduled, as we don't both to save its state.
 */
static void _kidle(void) {
	while (1) {
		arch_pause();
		switch_next();
	}
}

/**
 * @brief Release a process's paging data.
 *
 * If this is a thread in a POSIX process with other
 * living threads, the directory is not actually released
 * but the reference count for it is decremented.
 *
 * XXX There's probably no reason for this to take an argument;
 *     we only ever free directories in two places: on exec, or
 *     when a thread exits, and that's always the current thread.
 */
void process_release_directory(page_directory_t * dir) {
	spin_lock(dir->lock);
	dir->refcount--;
	if (dir->refcount < 1) {
		mmu_free(dir->directory);
		if (dir->mappings) {
			for (memmap_t * map = dir->mappings; map;) {
				memmap_t * next = map->next;
				if (map->file) fs_close_desc(map->file);
				free(map);
				map = next;
			}
			dir->mappings = NULL;
		}
		free(dir);
	} else {
		spin_unlock(dir->lock);
	}
}

static void setup_kernel_stack(process_t * proc) {
	proc->image.stack = (uintptr_t)mmu_map_module(KERNEL_STACK_SIZE) + KERNEL_STACK_SIZE;

	mmu_frame_allocate(
		mmu_get_page(proc->image.stack - KERNEL_STACK_SIZE, 0),
		MMU_FLAG_KERNEL);
}

process_t * spawn_kidle(int bsp) {
	process_t * idle = calloc(1,sizeof(process_t));
	idle->process = idle;
	idle->id = -1;
	idle->name = strdup("[kidle]");
	idle->flags = PROC_FLAG_IS_TASKLET | PROC_FLAG_STARTED | PROC_FLAG_RUNNING;
	elist_init(&idle->rq_node);
	setup_kernel_stack(idle);

	/* TODO arch_initialize_context(uintptr_t) ? */
	idle->thread.context.ip = (uintptr_t)&_kidle;
	idle->thread.context.sp = idle->image.stack;
	idle->thread.context.bp = idle->image.stack;

	this_core->time_total = 0;
	this_core->time_sys = 0;

	/* FIXME Why does the idle thread have wait queues and shm mappings?
	 *       Can we make sure these are never referenced and not allocate them? */
	gettimeofday(&idle->start, NULL);
	idle->thread.page_directory = calloc(1, sizeof(page_directory_t));
	idle->thread.page_directory->refcount = 1;
	idle->thread.page_directory->directory = mmu_clone(this_core->current_pml);
	idle->signals = NULL;
	spin_init(idle->thread.page_directory->lock);
	return idle;
}

process_t * spawn_init(void) {
	process_t * init = calloc(1,sizeof(process_t));

	process_tree = init;

	init->process = init;
	init->id         = 1;
	init->tgid       = 1;
	init->job        = 1;
	init->session    = 1;
	init->name       = strdup("init");
	init->cmdline    = NULL;
	init->user = init->real_user = init->saved_user = USER_ROOT_UID;
	init->user_group = init->real_user_group = init->saved_user_group = USER_ROOT_UID;
	init->mask       = 022;
	init->status     = 0;
	init->signals    = calloc(NUMSIGNALS+1, sizeof(struct signal_config));
	elist_init(&init->rq_node);
	waitable_queue_init(&init->child_queue);

	init->fds           = calloc(1, sizeof(fd_table_t));
	init->fds->refs     = 1;
	init->fds->length   = 0;
	init->fds->capacity = 4;
	init->fds->entries  = calloc(init->fds->capacity, sizeof(uintptr_t *));
	spin_init(init->fds->lock);

	init->wd = NULL;

	init->image.heap     = 0;
	setup_kernel_stack(init);

	init->flags         = PROC_FLAG_STARTED | PROC_FLAG_RUNNING;

	init->thread.page_directory = calloc(1, sizeof(page_directory_t));
	init->thread.page_directory->refcount = 1;
	init->thread.page_directory->directory = mmu_clone(NULL);
	mmu_set_directory(init->thread.page_directory->directory);
	spin_init(init->thread.page_directory->lock);
	list_insert(process_list, (void*)init);

	return init;
}

static process_t * spawn_process(process_t * parent, int flags, int close_at_fork) {
	process_t * proc = calloc(1,sizeof(process_t));

	proc->id          = get_next_pid();
	proc->tgid        = proc->id;
	proc->name        = strdup(parent->name);
	proc->owner = -1;

	elist_init(&proc->rq_node);
	waitable_queue_init(&proc->child_queue);

	proc->user        = parent->user;
	proc->real_user   = parent->real_user;
	proc->user_group  = parent->user_group;
	proc->real_user_group = parent->real_user_group;
	proc->saved_user = parent->saved_user;
	proc->saved_user_group = parent->saved_user_group;
	proc->mask        = parent->mask;
	proc->job         = parent->job;
	proc->session     = parent->session;
	proc->affinity    = parent->affinity;

	if (parent->supplementary_group_count) {
		proc->supplementary_group_count = parent->supplementary_group_count;
		proc->supplementary_group_list = malloc(sizeof(gid_t) * proc->supplementary_group_count);
		for (int i = 0; i < proc->supplementary_group_count; ++i) {
			proc->supplementary_group_list[i] = parent->supplementary_group_list[i];
		}
	}

	proc->thread.context.sp = 0;
	proc->thread.context.bp = 0;
	proc->thread.context.ip = 0;

	/* Entry is only stored for reference. */
	proc->image.heap        = parent->image.heap;
	setup_kernel_stack(proc);

	if (flags & PROC_REUSE_FDS) {
		spin_lock(parent->fds->lock);
		proc->fds = parent->fds;
		proc->fds->refs++;
		spin_unlock(parent->fds->lock);
	} else {
		proc->fds = calloc(1, sizeof(fd_table_t));
		spin_init(proc->fds->lock);
		proc->fds->refs = 1;
		spin_lock(parent->fds->lock);
		proc->fds->length = parent->fds->length;
		proc->fds->capacity = parent->fds->capacity;
		proc->fds->entries = calloc(proc->fds->capacity, sizeof(uintptr_t));
		for (uint32_t i = 0; i < parent->fds->length; ++i) {
			if (!parent->fds->entries[i]) continue;
			if (close_at_fork && (parent->fds->entries[i] & FD_PTR_CLOFORK)) continue;
			proc->fds->entries[i] = fs_clone_desc(FD_PTR_MASK(parent->fds->entries[i]), (parent->fds->entries[i] & 3) << 4);
		}
		spin_unlock(parent->fds->lock);
	}

	proc->exe = parent->exe ? (void*)fs_clone_desc(parent->exe, 0) : 0;
	proc->wd  = parent->wd  ? (void*)fs_clone_desc(parent->wd, 0) : 0;

	gettimeofday(&proc->start, NULL);

	spin_lock(tree_lock);
	etree_insert_child(parent, proc, tree_node);
	list_insert(process_list, (void*)proc);
	spin_unlock(tree_lock);
	return proc;
}

void process_reap(process_t * proc) {
	if (proc->tracees) {
		while (proc->tracees->length) {
			free(list_pop(proc->tracees));
		}
		free(proc->tracees);
	}

	if (proc == proc->process) {
		free(proc->signals);
	}

	/* Then unmap the pages entirely */
	mmu_unmap_module(proc->image.stack - KERNEL_STACK_SIZE, KERNEL_STACK_SIZE);

	process_release_directory(proc->thread.page_directory);

	if (proc->sig_queue) {
		list_destroy(proc->sig_queue);
		list_free(proc->sig_queue);
		free(proc->sig_queue);
	}

	process_free_cmdline(proc, NULL);

	free(proc->name);
	free(proc);
}

static int process_is_owned(process_t * proc) {
	for (int i = 0; i < processor_count; ++i) {
		if (processor_local_data[i].previous_process == proc ||
		    processor_local_data[i].current_process == proc) {
			return 1;
		}
	}
	return 0;
}

void process_reap_later(process_t * proc) {
	spin_lock(reap_lock);
	/* See if we can delete anything */
	while (reap_queue->head) {
		process_t * proc = reap_queue->head->value;
		if (!process_is_owned(proc)) {
			free(list_dequeue(reap_queue));
			process_reap(proc);
		} else {
			break;
		}
	}
	/* And delete this thing later */
	list_insert(reap_queue, proc);
	spin_unlock(reap_lock);
}

void process_acquire_big_lock(void) {
	spin_lock(reap_lock);
}

void process_release_big_lock(void) {
	spin_unlock(reap_lock);
}

/**
 * @brief Remove a process from the valid process list.
 *
 * Deletes a process from both the valid list and the process tree.
 * Any the process has any children, they become orphaned and are
 * moved under 'init', which is awoken if it was blocked on 'waitpid'.
 *
 * Finally, the process is freed.
 */
void process_delete(process_t * proc) {
	assert(proc != this_core->current_process);

	if (proc == process_tree) {
		dprintf("tried to delete init\n");
		return;
	}

	spin_lock(tree_lock);
	int has_children = !!proc->tree_node.et_children;

	etree_detach_reparent(proc, process_tree, tree_node);

	node_t * n = list_find(process_list, proc);
	list_delete(process_list, n);
	free(n);
	spin_unlock(tree_lock);

	if (has_children) {
		/* Wake up init */
		waitable_awaken_all(&process_tree->child_queue, (void*)(uintptr_t)proc->id);
	}

	// FIXME bitset_clear(&pid_set, proc->id);

	if (proc->supplementary_group_list) {
		proc->supplementary_group_count = 0;
		free(proc->supplementary_group_list);
	}

	/* Is someone using this process? */
	process_reap_later(proc);
}

extern void arch_wakeup_other(int id);

static int most_idle_of(uint64_t cpus) {
	int cpu = 0;
	int best = -1;

	for (int i = 0; i < processor_count; ++i) {
		if (cpus && !((1UL << i) & cpus)) continue;
		if (processor_local_data[i].kernel_idle_task->usage[0] >= best) {
			best = processor_local_data[i].kernel_idle_task->usage[0];
			cpu = i;
		}
	}

	return cpu;
}

static inline int queue_is_empty(struct elist * queue) {
	return queue == queue->el_next;
}

static inline int proc_on_cpu(process_t * proc, int cpu) {
	if (cpu < 0) return 0;
	if (!proc->affinity) return 1;
	if ((1UL << cpu) & proc->affinity) return 1;
	return 0;
}

static void pick_core_and_wakeup(process_t * proc) {
	if (processor_count == 1) {
		elist_insert_tail(&processor_local_data[0].readyqueue, &proc->rq_node);
		return;
	}

	int target;

	if (proc->affinity && (proc->affinity ^ (proc->affinity - 1)) >= proc->affinity) { /* popcount(affinity) == 1 */
		/* Pinned to one core, always use that one. */
		target = __builtin_ctzl(proc->affinity);
	} else if (proc_on_cpu(proc, proc->owner) && (proc->usage[0] > 500 || queue_is_empty(&processor_local_data[proc->owner].readyqueue))) {
		/* Prefer last CPU if it's empty, or if we're hogging it. */
		target = proc->owner;
	} else {
		/* Schedule on the least busy CPU. */
		target = most_idle_of(proc->affinity);
	}

	elist_insert_tail(&processor_local_data[target].readyqueue, &proc->rq_node);
	arch_wakeup_other(target);
}

/**
 * @brief Place an available process in the ready queue.
 *
 * Marks a process as available for general scheduling.
 * If the process was currently in a sleep queue, it is
 * marked as having been interrupted and removed from its
 * owning queue before being moved.
 *
 * The process must not otherwise have been in a scheduling
 * queue before it is placed in the ready queue.
 */
void make_process_ready(process_t * proc) {
	spin_lock(process_queue_lock);

	if (proc->rq_node.el_next != &proc->rq_node) {
		/* Already scheduled */
		spin_unlock(process_queue_lock);
		return;
	}

	pick_core_and_wakeup(proc);

	spin_unlock(process_queue_lock);
}

/**
 * @brief Pop the next available process from the queue.
 *
 * Gets the next available process from the round-robin scheduling
 * queue. If there is no process to run, the idle task is returned.
 */
process_t * next_ready_process(void) {
	spin_lock(process_queue_lock);

	if (!elist_peek_obj(this_core->readyqueue_p, process_t, rq_node)) {
		spin_unlock(process_queue_lock);
		return this_core->kernel_idle_task;
	}

	process_t * next = elist_pop_obj(this_core->readyqueue_p, process_t, rq_node);

	if ((next->flags & PROC_FLAG_RUNNING) && (next->owner != this_core->cpu_id)) {
		/* We pulled a process too soon, switch to idle for a bit so the
		 * core that marked this process as ready can finish switching away from it. */
		pick_core_and_wakeup(next);
		spin_unlock(process_queue_lock);
		return this_core->kernel_idle_task;
	}

	if (!(next->flags & PROC_FLAG_FINISHED)) {
		__sync_or_and_fetch(&next->flags, PROC_FLAG_RUNNING);
	}

	next->owner = this_core->cpu_id;
	spin_unlock(process_queue_lock);
	return next;
}

/**
 * @brief Indicates whether a process is ready to be run but not currently running.
 */
int process_is_ready(process_t * proc) {
	return (proc->rq_node.el_next != &proc->rq_node && !(proc->flags & PROC_FLAG_RUNNING));
}

/**
 * @brief Wake up processes that were sleeping on timers.
 *
 * Reschedule all processes whose timed waits have expired as of
 * the time indicated by @p seconds and @p subseconds. If the sleep
 * was part of an fswait system call timing out, the call is marked
 * as timed out before the process is rescheduled.
 */
void wakeup_sleepers(unsigned long seconds, unsigned long subseconds) {
	spin_lock(sleep_lock);
	for (struct elist * elem = sleep_list.el_next; elem != &sleep_list;) {
		struct timer_sleep * t = (void*)((char*)elem - offsetof(struct timer_sleep, el));
		if (!(t->end_tick < seconds || (t->end_tick == seconds && t->end_subtick <= subseconds))) break;
		t->callback(t, (void*)1);
		struct elist * next = elem->el_next;
		__elist_del(elem);
		elem = next;
	}
	spin_unlock(sleep_lock);
	return;
}

static int sleep_callback(struct timer_sleep *t, void * key) {
	/* mark t as sleep completed? */
	t->key = key;
	make_process_ready(t->priv);
	return 0;
}

/**
 * @brief Wait until a given time.
 *
 * Suspends the current process until the given time. The process may
 * still be resumed by a signal or other mechanism, in which case the
 * sleep will not be resumed by the kernel.
 */
int sleep_until(process_t * process, unsigned long seconds, unsigned long subseconds) {
	struct timer_sleep t;
	t.callback = sleep_callback;
	t.priv = process;
	t.end_tick = seconds;
	t.end_subtick = subseconds;
	t.key = NULL;
	elist_init(&t.el);

	spin_lock(sleep_lock);
	struct elist * before = &sleep_list;
	for (struct elist * elem = sleep_list.el_next; elem != &sleep_list; elem = elem->el_next) {
		struct timer_sleep * _t = (void*)((char*)elem - offsetof(struct timer_sleep, el));
		if (_t->end_tick > seconds || (_t->end_tick == seconds && _t->end_subtick > subseconds)) break;
		before = elem;
	}
	elist_insert_after(before, &t.el);
	spin_unlock(sleep_lock);

	switch_task(0);

	spin_lock(sleep_lock);
	__elist_del(&t.el);
	spin_unlock(sleep_lock);

	return t.key == (void*)1;
}

static int process_compare(void * proc_v, void * pid_v) {
	pid_t pid = (*(pid_t *)pid_v);
	process_t * proc = (process_t *)proc_v;

	return (uint8_t)(proc->id == pid);
}

process_t * process_from_pid(pid_t pid) {
	if (pid < 0) return NULL;
	spin_lock(tree_lock);
	process_t * proc = etree_find(process_tree, tree_node, &pid, process_compare);
	spin_unlock(tree_lock);
	return proc;
}


long process_move_fd(process_t * proc, long src, long dest, int forbid_noop, int flags) {
	if ((size_t)src >= proc->fds->length || dest < -1) return -EBADF;
	if (!proc->fds->entries[src]) return -EBADF;
	if (dest == src) return forbid_noop ? -EINVAL : dest;
	if (dest == -1) {
		uintptr_t fd_flags = 0;
		if (flags & PROC_FD_MODE_CLOEXEC) fd_flags |= FD_PTR_CLOEXEC;
		if (flags & PROC_FD_MODE_CLOFORK) fd_flags |= FD_PTR_CLOFORK;
		return process_append_fd(proc, FD_PTR_MASK(fs_clone_desc(FD_PTR_MASK(proc->fds->entries[src]), 0)), fd_flags);
	}
	if ((size_t)dest >= proc->fds->length) {
		return process_fd_dup_least(proc, src, dest, flags);
	}
	spin_lock(proc->fds->lock);
	if (proc->fds->entries[dest]) {
		fs_close_desc(FD_PTR_MASK(proc->fds->entries[dest]));
		proc->fds->entries[dest] = 0;
	}
	process_fds_copy(proc, src, dest, flags);
	spin_unlock(proc->fds->lock);
	return dest;
}

void tasking_start(void) {
	this_core->current_process = spawn_init();
	this_core->kernel_idle_task = spawn_kidle(1);
}

static int wait_candidate(process_t * parent, int pid, int options, process_t * proc) {
	if (!proc) return 0;

	if (options & WNOKERN) {
		/* Skip kernel processes */
		if (proc->flags & PROC_FLAG_IS_TASKLET) return 0;
	}

	if (pid < -1) {
		if (proc->job == -pid || proc->id == -pid) return 1;
	} else if (pid == 0) {
		/* Matches our group ID */
		if (proc->job == parent->job) return 1;
	} else if (pid > 0) {
		/* Specific pid */
		if (proc->id == pid) return 1;
	} else {
		return 1;
	}
	return 0;
}

int waitpid(int pid, int * status, int options) {
	process_t * proc = (process_t*)this_core->current_process;

	do {
		process_t * candidate = NULL;
		int has_children = 0;
		int is_parent = 0;
		int candidate_from_trace = 0;

		spin_lock(proc->wait_lock);

		/* First, find out if there is anyone to reap */
		etree_children_foreach(child, proc->process, tree_node) {
			if (child == proc) continue; /* Don't allow a thread to wait on itself */

			if (wait_candidate(proc->process, pid, options, child)) {
				has_children = 1;
				if (child->flags & PROC_FLAG_FINISHED) {
					is_parent = 1;
					candidate = child;
					break;
				}
				if ((child->flags & PROC_FLAG_SUSPENDED) && ((child->status & 0xFF) == 0x7F)) {
					int reason = (child->status >> 16) & 0xFF;
					if ((options & WSTOPPED) || (reason == 0xFF && (options & WUNTRACED))) {
						is_parent = 1;
						candidate = child;
						break;
					}
				}
			}
		}

		if (!candidate && proc->tracees) {
			foreach(node, proc->tracees) {
				process_t * child = node->value;
				if (wait_candidate(proc,pid,options,child)) {
					has_children = 1;
					if (child->flags & (PROC_FLAG_SUSPENDED | PROC_FLAG_FINISHED)) {
						candidate_from_trace = 1;
						candidate = child;
						break;
					}
				}
			}
		}

		if (!has_children) {
			/* No valid children matching this description */
			spin_unlock(proc->wait_lock);
			return -ECHILD;
		}

		if (candidate) {
			if (status) {
				*status = *(volatile int*)&candidate->status;
			}
			int pid = candidate->id;
			if (is_parent) candidate->status &= ~0xFF;
			spin_unlock(proc->wait_lock);
			if (is_parent && (candidate->flags & PROC_FLAG_FINISHED)) {
				while (*((volatile int *)&candidate->flags) & PROC_FLAG_RUNNING);
				proc->time_children += candidate->time_children + candidate->time_total;
				proc->time_sys_children += candidate->time_sys_children + candidate->time_sys;
				process_delete((process_t*)candidate);
			}
			if (candidate_from_trace && candidate->tracer != proc->id) {
				ptrace_untrace((process_t*)proc, (process_t*)candidate);
			}
			return pid;
		} else {
			if (options & WNOHANG) {
				spin_unlock(proc->wait_lock);
				return 0;
			}
			/* Wait */
			if (waitable_await_unlocking(&proc->process->child_queue, &proc->wait_lock)) {
				return -EINTR;
			}
		}
	} while (1);
}

void process_awaken_signal(process_t * process) {
	make_process_ready(process);
}

process_t * process_get_parent(process_t * process) {
	spin_lock(tree_lock);
	process_t * result = etree_parent(process, tree_node);
	spin_unlock(tree_lock);
	return result;
}

void task_exit(long retval) {
	this_core->current_process->status = retval & 0xFFFF;

	if (this_core->current_process->process == this_core->current_process) {
		/* If is thread leader, kill threads */
		pid_t * children = NULL;
		size_t thread_count = process_collect_by(offsetof(process_t,tgid), sizeof(pid_t), (void*)&this_core->current_process->tgid, &children, 1);
		for (size_t i = 0; i < thread_count; ++i) {
			if (children[i] == this_core->current_process->tgid) continue;
			send_signal(children[i], SIGKILL, 1);
		}
		if (children) free(children);
	}

	if (this_core->current_process->fds) {
		spin_lock(this_core->current_process->fds->lock);
		this_core->current_process->fds->refs--;
		if (this_core->current_process->fds->refs == 0) {
			for (uint32_t i = 0; i < this_core->current_process->fds->length; ++i) {
				if (this_core->current_process->fds->entries[i]) {
					fs_close_desc(FD_PTR_MASK(this_core->current_process->fds->entries[i]));
					this_core->current_process->fds->entries[i] = 0;
				}
			}
			free(this_core->current_process->fds->entries);
			free(this_core->current_process->fds);
			this_core->current_process->fds = NULL;
		} else {
			spin_unlock(this_core->current_process->fds->lock);
		}
	}

	if (this_core->current_process->exe) {
		fs_close_desc(this_core->current_process->exe);
		this_core->current_process->exe = NULL;
	}
	if (this_core->current_process->wd) {
		fs_close_desc(this_core->current_process->wd);
		this_core->current_process->wd = NULL;
	}

	if (this_core->current_process->tracees) {
		spin_lock(this_core->current_process->wait_lock);
		while (this_core->current_process->tracees->length) {
			node_t * n = list_pop(this_core->current_process->tracees);
			process_t * tracee = n->value;
			free(n);
			if (is_valid_process(tracee)) {
				if (tracee->tracer == this_core->current_process->id) {
					tracee->tracer = 0;
					__sync_and_and_fetch(&tracee->flags, ~(PROC_FLAGS_TRACE));
					if (tracee->flags & PROC_FLAG_SUSPENDED) {
						__sync_and_and_fetch(&tracee->flags, ~(PROC_FLAG_SUSPENDED));
						make_process_ready(tracee);
					}
				}
			}
		}
		spin_unlock(this_core->current_process->wait_lock);
	}

	update_process_times();

	process_t * parent = process_get_parent((process_t *)this_core->current_process);
	__sync_or_and_fetch(&this_core->current_process->flags, PROC_FLAG_FINISHED);

	if (this_core->current_process->tracer) {
		process_t * tracer = process_from_pid(this_core->current_process->tracer);
		this_core->current_process->tracer = 0;
		if (tracer && tracer != parent) {
			spin_lock(tracer->wait_lock);
			waitable_awaken_all(&tracer->process->child_queue, (void*)(uintptr_t)this_core->current_process->id);
			spin_unlock(tracer->wait_lock);
		}
	}

	process_send_sigchld((process_t*)this_core->current_process, parent,
		(retval & 0xFF) ? CLD_KILLED : CLD_EXITED,
		(retval & 0xFF) ? (retval & 0xFF) : (retval >> 8));

	switch_next();
}

#define PUSH(stack, type, item) stack -= sizeof(type); \
							*((volatile type *) stack) = item

static char ** dup_cmdline(process_t * proc) {
	if (!proc->cmdline) return NULL;

	size_t sz = 0;
	while (proc->cmdline[sz]) sz++;

	char ** cmdline = calloc(sz + 1, sizeof(char*));
	for (size_t j = 0; j < sz; ++j) {
		cmdline[j] = strdup(proc->cmdline[j]);
	}

	return cmdline;
}

void process_free_cmdline(process_t * proc, char **ncmd) {
	char ** old = proc->cmdline;
	proc->cmdline = ncmd;

	if (!old) return;

	for (int j = 0; old[j]; j++) {
		free(old[j]);
	}
	free(old);
}

pid_t fork(void) {
	uintptr_t sp, bp;
	process_t * parent = (process_t*)this_core->current_process;
	union PML * directory = mmu_clone(parent->thread.page_directory->directory);
	process_t * new_proc = spawn_process(parent->process, 0, 1);
	new_proc->process = new_proc;
	new_proc->pty = parent->process->pty;
	new_proc->thread.page_directory = calloc(1, sizeof(page_directory_t));
	new_proc->thread.page_directory->refcount = 1;
	new_proc->thread.page_directory->directory = directory;
	new_proc->cmdline = dup_cmdline(parent);

	memmap_t * prev = NULL;
	for (memmap_t * maps = parent->thread.page_directory->mappings; maps; maps = maps->next) {
		memmap_t * nmap = calloc(1, sizeof(memmap_t));
		nmap->base = maps->base;
		nmap->length = maps->length;
		nmap->flags = maps->flags;
		nmap->prot = maps->prot;
		nmap->owner = new_proc->thread.page_directory;

		if (maps->file) {
			nmap->file = (void*)fs_clone_desc(maps->file, 0);
			nmap->offset = maps->offset;
		}

		if (!prev) {
			new_proc->thread.page_directory->mappings = nmap;
		} else {
			prev->next = nmap;
			nmap->prev = prev;
		}

		prev = nmap;
	}

	spin_init(new_proc->thread.page_directory->lock);

	new_proc->signals = calloc(NUMSIGNALS + 1, sizeof(struct signal_config));
	memcpy(new_proc->signals, parent->signals, sizeof(struct signal_config) * (NUMSIGNALS+1));
	new_proc->blocked_signals = parent->blocked_signals;

	arch_save_floating(new_proc);

	struct regs r;
	memcpy(&r, parent->syscall_registers, sizeof(struct regs));
	sp = new_proc->image.stack;
	bp = sp;

	uregs_syscall_result(&r) = 0;
	PUSH(sp, struct regs, r);

	new_proc->syscall_registers = (void*)sp;
	new_proc->thread.context.sp = sp;
	new_proc->thread.context.bp = bp;
	new_proc->thread.context.tls_base = parent->thread.context.tls_base;
	new_proc->thread.context.ip = (uintptr_t)&arch_resume_user;
	arch_save_context(&parent->thread);
	memcpy(new_proc->thread.context.saved, parent->thread.context.saved, sizeof(parent->thread.context.saved));

	if (parent->flags & PROC_FLAG_IS_TASKLET) new_proc->flags |= PROC_FLAG_IS_TASKLET;
	if ((parent->flags & PROC_FLAG_TRACE_FORK) && parent->tracer) ptrace_trace(process_from_pid(parent->tracer), new_proc);

	make_process_ready(new_proc);
	return new_proc->id;
}

pid_t clone(uintptr_t new_stack, uintptr_t thread_func, uintptr_t arg) {
	uintptr_t sp, bp;
	process_t * parent = (process_t *)this_core->current_process;
	process_t * new_proc = spawn_process(parent->process, PROC_REUSE_FDS, 0);
	new_proc->process = parent->process;
	new_proc->tgid = parent->process->id;
	new_proc->thread.page_directory = this_core->current_process->thread.page_directory;
	spin_lock(new_proc->thread.page_directory->lock);
	new_proc->thread.page_directory->refcount++;
	spin_unlock(new_proc->thread.page_directory->lock);

	new_proc->signals = parent->process->signals;
	new_proc->blocked_signals = parent->blocked_signals;

	arch_save_floating(new_proc);

	struct regs r;
	memcpy(&r, parent->syscall_registers, sizeof(struct regs));
	sp = new_proc->image.stack;
	bp = sp;

	PUSH(sp, struct regs, r);
	new_proc->syscall_registers = (void*)sp;
	arch_prepare_thread_entry(new_proc->syscall_registers, arg, thread_func, new_stack);
	new_proc->thread.context.sp = sp;
	new_proc->thread.context.bp = bp;
	new_proc->thread.context.tls_base = this_core->current_process->thread.context.tls_base;
	new_proc->thread.context.ip = (uintptr_t)&arch_resume_user;

	if (parent->flags & PROC_FLAG_IS_TASKLET) new_proc->flags |= PROC_FLAG_IS_TASKLET;
	if ((parent->flags & PROC_FLAG_TRACE_CLONE) && parent->tracer) ptrace_trace(process_from_pid(parent->tracer), new_proc);

	make_process_ready(new_proc);
	return new_proc->id;
}

process_t * spawn_worker_thread(void (*entrypoint)(void * argp), const char * name, void * argp) {
	process_t * proc = calloc(1,sizeof(process_t));

	proc->flags = PROC_FLAG_IS_TASKLET | PROC_FLAG_STARTED;

	proc->process     = proc;
	proc->id          = get_next_pid();
	proc->tgid        = proc->id;
	proc->name        = strdup(name);
	proc->cmdline     = NULL;
	elist_init(&proc->rq_node);
	waitable_queue_init(&proc->child_queue);

	proc->job         = proc->id;
	proc->session     = proc->id;

	proc->signals = calloc(NUMSIGNALS+1, sizeof(struct signal_config));

	proc->thread.page_directory = calloc(1, sizeof(page_directory_t));
	proc->thread.page_directory->refcount = 1;
	proc->thread.page_directory->directory = mmu_clone(mmu_get_kernel_directory());
	spin_init(proc->thread.page_directory->lock);

	setup_kernel_stack(proc);

	PUSH(proc->image.stack, uintptr_t, (uintptr_t)entrypoint);
	PUSH(proc->image.stack, void*, argp);

	proc->thread.context.sp = proc->image.stack;
	proc->thread.context.bp = proc->image.stack;
	proc->thread.context.ip = (uintptr_t)&arch_enter_tasklet;

	gettimeofday(&proc->start, NULL);

	spin_lock(tree_lock);
	etree_insert_child(this_core->current_process, proc, tree_node);
	list_insert(process_list, (void*)proc);
	spin_unlock(tree_lock);

	make_process_ready(proc);

	return proc;
}

static void update_one_process(uint64_t clock_ticks, uint64_t perf_scale, process_t * proc) {
	proc->usage[3] = proc->usage[2];
	proc->usage[2] = proc->usage[1];
	proc->usage[1] = proc->usage[0];
	proc->usage[0] = (1000 * (proc->time_total - proc->time_prev)) / (clock_ticks * perf_scale);
	proc->time_prev = proc->time_total;
}

void update_process_usage(uint64_t clock_ticks, uint64_t perf_scale) {
	spin_lock(tree_lock);
	foreach(lnode, process_list) {
		process_t * proc = lnode->value;
		update_one_process(clock_ticks, perf_scale, proc);
	}
	spin_unlock(tree_lock);
	/* Now use idle tasks to calculator processor activity? */
	for (int i = 0; i < processor_count; ++i) {
		process_t * proc = processor_local_data[i].kernel_idle_task;
		update_one_process(clock_ticks, perf_scale, proc);
	}
}

/**
 * @brief Collect a list of processes matching a particular value.
 *
 * The process list is volatile and must be locked. If you want to
 * perform an operation on a bunch of pids that may, itself, need
 * to access the process list, this function might help. It compares
 * the requested offset of the process struct with the given target
 * value of all thread group leaders, and collects the PIDs of those
 * matching processes into a dynamically resized buffer, returning
 * the number of processes added.
 *
 * @param field Offset of the field in procces_t to compare against.
 * @param fieldSize Size of the field to compare against.
 * @param target Field value you want to match.
 * @param into Place you want the resulting array pointer to be stored.
 * @param threads Whether to include threads.
 * @returns Count of collected PIDs.
 */
size_t process_collect_by(off_t field, size_t fieldSize, void * target, pid_t ** into, int threads) {
	size_t count = 0;
	size_t cap = 0;
	*into = NULL;

	spin_lock(tree_lock);
	foreach(node, process_list) {
		process_t * proc = node->value;
		if (!threads && proc->tgid != proc->id) continue; /* Only collect thread group leaders */
		if (!memcmp((char*)proc + field, target, fieldSize)) {
			if (count == cap) {
				cap = cap ? cap * 2 : 16;
				*into = realloc(*into, cap * sizeof(pid_t));
			}
			(*into)[count++] = proc->id;
		}
	}
	spin_unlock(tree_lock);

	return count;
}

/**
 * @brief Send a signal to multiple processes.
 *
 * Similar to @c send_signal but for when a negative PID needs to be used.
 *
 * @param group The group process ID. Positive PID, not negative.
 * @param signal Signal number to deliver.
 * @param force_root See explanation in @c send_signal
 * @returns 0 if something was killed, -ESRCH otherwise.
 */
int group_send_signal(pid_t group, int signal, int force_root) {
	int kill_self = 0;
	int killed_something = 0;

	if (signal >= NUMSIGNALS || signal < 0) return -EINVAL;

	/* First collect a list of things to signal */
	pid_t * pids = NULL;
	size_t count = process_collect_by(offsetof(process_t,job),sizeof(pid_t),&group,&pids, 0);

	for (size_t i = 0; i < count; ++i) {
		if (pids[i] == this_core->current_process->tgid) kill_self = 1;
		else if (send_signal(pids[i], signal, force_root) == 0) killed_something = 1;
	}

	if (pids) free(pids);

	if (kill_self && send_signal(this_core->current_process->tgid, signal, force_root) == 0) killed_something = 1;
	return killed_something ? 0 : -ESRCH;
}

/**
 * @brief Send a signal to multiple processes, by session ID.
 *
 * This is mostly used by the TTY layer to handle hangups.
 *
 * @param session The session ID to send to.
 * @param signal Signal number to deliver.
 * @param force_root See explanation in @c send_signal
 * @returns 0 if something was killed, -ESRCH otherwise.
 */
int session_send_signal(pid_t session, int signal, int force_root) {
	int kill_self = 0;
	int killed_something = 0;

	if (signal >= NUMSIGNALS || signal < 0) return -EINVAL;

	/* First collect a list of things to signal */
	pid_t * pids = NULL;
	size_t count = process_collect_by(offsetof(process_t,session),sizeof(pid_t),&session,&pids, 0);

	for (size_t i = 0; i < count; ++i) {
		if (pids[i] == this_core->current_process->tgid) kill_self = 1;
		else if (send_signal(pids[i], signal, force_root) == 0) killed_something = 1;
	}

	if (pids) free(pids);

	if (kill_self && send_signal(this_core->current_process->tgid, signal, force_root) == 0) killed_something = 1;
	return killed_something ? 0 : -ESRCH;
}

int process_close_fd(process_t * proc, int fd) {
	int out = 0;
	spin_lock(proc->fds->lock);
	if (fd < 0 || (size_t)fd >= proc->fds->length) {
		out = -EBADF;
		goto _done;
	}

	if (!proc->fds->entries[fd]) {
		out = -EBADF;
		goto _done;
	}

	fs_close_desc(FD_PTR_MASK(proc->fds->entries[fd]));
	proc->fds->entries[fd] = 0;

_done:
	spin_unlock(proc->fds->lock);
	return out;
}

int process_close_fds(process_t * proc, int for_what) {
	spin_lock(proc->fds->lock);
	for (unsigned int i = 0; i < proc->fds->length; ++i) {
		if (proc->fds->entries[i] && (proc->fds->entries[i] & (for_what >> 4))) {
			fs_close_desc(FD_PTR_MASK(proc->fds->entries[i]));
			proc->fds->entries[i] = 0;
		}
	}
	if (proc->exe) {
		fs_close_desc(proc->exe);
		proc->exe = 0;
	}
	spin_unlock(proc->fds->lock);
	return 0;
}

void process_send_sigchld(process_t * proc, process_t * parent, int reason, int status) {
	if (parent && !(parent->flags & PROC_FLAG_FINISHED)) {
		spin_lock(parent->wait_lock);
		siginfo_t cause = {0};
		cause.si_code = reason;
		cause.si_pid = proc->id;
		cause.si_uid = proc->real_user;
		cause.si_status = status;

		waitable_awaken_all(&parent->child_queue, (void*)(uintptr_t)proc->id);
		send_signal_info(parent->tgid, SIGCHLD, 1, &cause);

		spin_unlock(parent->wait_lock);
	}
}

size_t process_erase_field(off_t field, size_t fieldSize, void * target) {
	size_t count = 0;
	spin_lock(tree_lock);
	foreach(node, process_list) {
		process_t * proc = node->value;
		if (!memcmp((char*)proc + field, target, fieldSize)) {
			memset((char*)proc + field, 0, fieldSize);
			count++;
		}
	}
	spin_unlock(tree_lock);

	return count;
}

size_t process_get_tty(process_t * proc, size_t len, char * out) {
	spin_lock(tree_lock);
	size_t ret = proc->process->pty ? proc->process->pty->fill_name(proc->process->pty, len, out) : 0;
	spin_unlock(tree_lock);
	return ret;
}

struct pollable_waiter {
	struct waitable_waiter _waiter;
	struct waitable_queue * queue;
};

struct poll_table_page {
	struct poll_table_page * next;
	struct pollable_waiter * entry;
	struct pollable_waiter entries[];
};

struct poll_table {
	process_t * proc;
	spin_lock_t lock;
	int accepting;
	size_t cnt;
	struct poll_table_page * next;
	struct pollable_waiter waiters[10];
};

static int poll_wake(struct waitable_waiter *waiter, void *key) {
	struct poll_table * pt = waiter->priv;
	process_t * proc = pt->proc;
	waiter->key = key;
	make_process_ready(proc);
	return 1;
}

int add_poll_wait(void * poll_table, struct waitable_queue * queue) {
	struct poll_table * pt = poll_table;
	if (!pt->accepting) return 0;

	spin_lock(pt->lock);
	struct pollable_waiter * w;

	if (pt->cnt < 10) {
		w = &pt->waiters[pt->cnt++];
	} else {
		if (!pt->next || ((uintptr_t)(pt->next->entry + 1) > (uintptr_t)pt->next + 4096)) {
			uintptr_t index = mmu_allocate_a_frame();
			struct poll_table_page * np = mmu_map_from_physical(index << 12);
			np->entry = np->entries;
			np->next = pt->next;
			pt->next = np;
		}
		w = pt->next->entry++;
	}

	memset(w, 0, sizeof(struct pollable_waiter));
	waitable_waiter_init((void*)w, poll_wake, poll_table);
	w->queue = queue;
	spin_unlock(pt->lock);

	spin_lock(queue->lock);
	elist_insert_tail(&queue->queue, &w->_waiter.list_node);
	spin_unlock(queue->lock);

	return 0;
}

static inline void poll_init(struct poll_table *pt) {
	pt->proc = this_core->current_process;
	pt->cnt = 0;
	pt->accepting = 1;
	pt->next = NULL;
	spin_init(pt->lock);
}

static inline void poll_cleanup(struct poll_table *pt) {
	for (size_t i = 0; i < pt->cnt; ++i) {
		struct pollable_waiter * w = &pt->waiters[i];
		spin_lock(w->queue->lock);
		__elist_del(&w->_waiter.list_node);
		spin_unlock(w->queue->lock);
	}
	struct poll_table_page * n = pt->next;
	while (n) {
		struct poll_table_page *next = n->next;
		struct pollable_waiter * w = n->entry;
		do {
			w--;
			spin_lock(w->queue->lock);
			__elist_del(&w->_waiter.list_node);
			spin_unlock(w->queue->lock);
		} while (w > n->entries);
		mmu_frame_release(mmu_map_to_physical(this_core->current_pml, (uintptr_t)n));
		n = next;
	}
}

int do_fswait3(int c, int fds[], int timeout, int out[]) {
	unsigned long target_ticks = 0, target_subticks = 0;
	if (timeout > 0) relative_time(0, timeout * 1000, &target_ticks, &target_subticks);

	int has_match = -1;
	for (int i = 0; i < c; ++i) {
		if (!FD_CHECK(fds[i])) return -EBADF;
		if ((out[i] = (selectcheck_fs(FD_ENTRY(fds[i])) == 0)) && has_match == -1) has_match = i;
	}

	/* Already found a match, return immediately with the first match */
	if (has_match != -1) return has_match;
	if (timeout == 0)  return c;

	/* Prep sleepy boys */
	while (1) {
		struct poll_table pt;
		poll_init(&pt);

		for (int i = 0; i < c; ++i) {
			if (!FD_CHECK(fds[i])) continue;
			selectwait_fs(FD_ENTRY(fds[i]), &pt);
		}

		int timed_out = 0;
		if (timeout != -1) timed_out = sleep_until(this_core->current_process, target_ticks, target_subticks);
		else switch_task(0);

		poll_cleanup(&pt);

		for (int i = 0; i < c; ++i) {
			if (!FD_CHECK(fds[i])) continue;
			if ((out[i] |= (selectcheck_fs(FD_ENTRY(fds[i])) == 0)) && has_match == -1) has_match = i;
		}

		if (has_match != -1) return has_match;
		if (timed_out) return c;

		if (has_pending()) return -EINTR;
	}
}

int do_poll(struct pollfd *fds, unsigned long count, int timeout) {
	/* Spot check file descriptors */
	for (unsigned long i = 0; i < count; ++i) {
		fds[i].revents = 0;
		if (fds[i].fd < 0) continue;
		if (!FD_CHECK(fds[i].fd)) fds[i].revents |= POLLNVAL;
	}

	unsigned long target_ticks = 0, target_subticks = 0;
	if (timeout > 0) relative_time(0, timeout * 1000, &target_ticks, &target_subticks);

	int timed_out = 0;
	int match = 0;

	while (1) {
		struct poll_table pt;
		poll_init(&pt);

		for (unsigned long i = 0; i < count; ++i) {
			if (fds[i].fd < 0) continue;
			if (!FD_CHECK(fds[i].fd)) continue;
			struct fs_file_description * f = FD_FILE(fds[i].fd);

			int mask = 0;
			if (f->inode->ops->poll) {
				mask = f->inode->ops->poll(f, &pt);
			} else if (f->inode->ops->selectwait) {
				f->inode->ops->selectwait(f->inode, &pt);
				int r = f->inode->ops->selectcheck(f->inode);
				if (!r) mask = POLLIN;
			}

			if ((mask | POLLHUP | POLLERR) & fds[i].events) {
				fds[i].revents |= ((mask | POLLHUP | POLLERR) & fds[i].events);
				match += 1;
				pt.accepting = 0;
			}
		}

		if (timed_out) timeout = 0;

		if (!match && timeout != 0) {
			if (timeout > 0) timed_out = sleep_until(this_core->current_process, target_ticks, target_subticks);
			else switch_task(0);
		}

		poll_cleanup(&pt);

		if (match || (!timed_out && timeout == 0)) return match;
		if (timed_out && timeout == 0) return 0;
		if (has_pending()) return -EINTR;
	}
}

int process_wait_nodes(process_t * process,fs_node_t * nodes[], int timeout) {
	unsigned long target_ticks = 0, target_subticks = 0;
	if (timeout > 0) relative_time(0, timeout * 1000, &target_ticks, &target_subticks);

	int index = 0;
	for (fs_node_t **n = nodes; *n; n++) {
		int result = selectcheck_fs(*n);
		if (result < 0) return -EBADF;
		if (result == 0) return index;
		index++;
	}

	if (timeout == 0) return index;

	/* Prep sleepy boys */
	while (1) {
		struct poll_table pt;
		poll_init(&pt);

		for (fs_node_t **n = nodes; *n; n++) selectwait_fs(*n, &pt);

		int timed_out = 0;
		if (timeout != -1) timed_out = sleep_until(this_core->current_process, target_ticks, target_subticks);
		else switch_task(0);

		poll_cleanup(&pt);

		index = 0;
		for (fs_node_t **n = nodes; *n; n++) {
			if (selectcheck_fs(*n) == 0) return index;
			index++;
		}

		if (timed_out) return index;

		if (has_pending()) return -EINTR;
	}
}

