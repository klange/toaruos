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
#include <errno.h>
#include <kernel/assert.h>
#include <kernel/process.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/vfs.h>
#include <kernel/spinlock.h>
#include <kernel/tree.h>
#include <kernel/list.h>
#include <kernel/mmu.h>
#include <kernel/shm.h>
#include <kernel/signal.h>
#include <kernel/time.h>
#include <kernel/misc.h>
#include <kernel/syscall.h>
#include <sys/wait.h>
#include <sys/signal_defs.h>

/* FIXME: This only needs the size of the regs struct... */
#if defined(__x86_64__)
#include <kernel/arch/x86_64/regs.h>
#elif defined(__aarch64__)
#include <kernel/arch/aarch64/regs.h>
#else
#error "no regs"
#endif

tree_t * process_tree;  /* Stores the parent-child process relationships; the root of this graph is 'init'. */
list_t * process_list;  /* Stores all existing processes. Mostly used for sanity checking or for places where iterating over all processes is useful. */
list_t * process_queue; /* Scheduler ready queue. This the round-robin source. The head is the next process to run. */
list_t * sleep_queue;   /* Ordered list of processes waiting to be awoken by timeouts. The head is the earliest thread to awaken. */
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

void update_process_times(int includeSystem) {
	uint64_t pTime = arch_perf_timer();
	if (this_core->current_process->time_in && this_core->current_process->time_in < pTime) {
		this_core->current_process->time_total +=  pTime - this_core->current_process->time_in;
	}
	this_core->current_process->time_in = 0;

	if (includeSystem) {
		if (this_core->current_process->time_switch && this_core->current_process->time_switch < pTime) {
			this_core->current_process->time_sys += pTime - this_core->current_process->time_switch;
		}
		this_core->current_process->time_switch = 0;
	}
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
	update_process_times(1);

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

extern void * _ret_from_preempt_source;

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

	if (this_core->current_process == this_core->kernel_idle_task && __builtin_return_address(0) != &_ret_from_preempt_source) {
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

/**
 * @brief Initial scheduler datastructures.
 *
 * Called by early system startup to allocate trees and lists
 * the schedule uses to track processes.
 */
void initialize_process_tree(void) {
	process_tree = tree_create();
	process_list = list_create("global process list",NULL);
	process_queue = list_create("global scheduler queue",NULL);
	sleep_queue = list_create("global timed sleep queue",NULL);
	reap_queue = list_create("processes awaiting later cleanup",NULL);

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
 * @brief Allocate a new file descriptor.
 *
 * Adds a new entry to the file descriptor table for @p proc
 * pointing to the file @p node. The file descriptor's offset
 * and file modes must be set by the caller afterwards.
 *
 * @param proc Process whose file descriptor should be modified.
 * @param node VFS object to add a reference to.
 * @returns the new file descriptor index
 */
unsigned long process_append_fd(process_t * proc, fs_node_t * node) {
	spin_lock(proc->fds->lock);
	/* Fill gaps */
	for (unsigned long i = 0; i < proc->fds->length; ++i) {
		if (!proc->fds->entries[i]) {
			proc->fds->entries[i] = node;
			/* modes, offsets must be set by caller */
			proc->fds->modes[i] = 0;
			proc->fds->offsets[i] = 0;
			spin_unlock(proc->fds->lock);
			return i;
		}
	}
	/* No gaps, expand */
	if (proc->fds->length == proc->fds->capacity) {
		proc->fds->capacity *= 2;
		proc->fds->entries = realloc(proc->fds->entries, sizeof(fs_node_t *) * proc->fds->capacity);
		proc->fds->modes   = realloc(proc->fds->modes,   sizeof(int) * proc->fds->capacity);
		proc->fds->offsets = realloc(proc->fds->offsets, sizeof(uint64_t) * proc->fds->capacity);
	}
	proc->fds->entries[proc->fds->length] = node;
	/* modes, offsets must be set by caller */
	proc->fds->modes[proc->fds->length] = 0;
	proc->fds->offsets[proc->fds->length] = 0;
	proc->fds->length++;
	spin_unlock(proc->fds->lock);
	return proc->fds->length-1;
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
	}
}

static void _kburn(void) {
	while (1) {
		arch_pause();
#ifndef __aarch64__
		switch_next();
#endif
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
		free(dir);
	} else {
		spin_unlock(dir->lock);
	}
}

process_t * spawn_kidle(int bsp) {
	process_t * idle = calloc(1,sizeof(process_t));
	idle->id = -1;
	idle->name = strdup("[kidle]");
	idle->flags = PROC_FLAG_IS_TASKLET | PROC_FLAG_STARTED | PROC_FLAG_RUNNING;
	idle->image.stack = (uintptr_t)valloc(KERNEL_STACK_SIZE)+ KERNEL_STACK_SIZE;
	mmu_frame_allocate(
		mmu_get_page(idle->image.stack - KERNEL_STACK_SIZE, 0),
		MMU_FLAG_KERNEL);

	/* TODO arch_initialize_context(uintptr_t) ? */
	idle->thread.context.ip = bsp ? (uintptr_t)&_kidle : (uintptr_t)&_kburn;
	idle->thread.context.sp = idle->image.stack;
	idle->thread.context.bp = idle->image.stack;

	/* FIXME Why does the idle thread have wait queues and shm mappings?
	 *       Can we make sure these are never referenced and not allocate them? */
	idle->wait_queue = list_create("process wait queue (kidle)",idle);
	idle->shm_mappings = list_create("process shm mappings (kidle)",idle);
	gettimeofday(&idle->start, NULL);
	idle->thread.page_directory = malloc(sizeof(page_directory_t));
	idle->thread.page_directory->refcount = 1;
	idle->thread.page_directory->directory = mmu_clone(this_core->current_pml);
	spin_init(idle->thread.page_directory->lock);
	return idle;
}

process_t * spawn_init(void) {
	process_t * init = calloc(1,sizeof(process_t));
	tree_set_root(process_tree, (void*)init);

	init->tree_entry = process_tree->root;
	init->id         = 1;
	init->group      = 0;
	init->job        = 1;
	init->session    = 1;
	init->name       = strdup("init");
	init->cmdline    = NULL;
	init->user       = USER_ROOT_UID;
	init->real_user  = USER_ROOT_UID;
	init->user_group = USER_ROOT_UID;
	init->real_user_group = USER_ROOT_UID;
	init->mask       = 022;
	init->status     = 0;

	init->fds           = malloc(sizeof(fd_table_t));
	init->fds->refs     = 1;
	init->fds->length   = 0;
	init->fds->capacity = 4;
	init->fds->entries  = malloc(init->fds->capacity * sizeof(fs_node_t *));
	init->fds->modes    = malloc(init->fds->capacity * sizeof(int));
	init->fds->offsets  = malloc(init->fds->capacity * sizeof(uint64_t));
	spin_init(init->fds->lock);

	init->wd_node = clone_fs(fs_root);
	init->wd_name = strdup("/");

	init->image.entry    = 0;
	init->image.heap     = 0;
	init->image.stack    = (uintptr_t)valloc(KERNEL_STACK_SIZE) + KERNEL_STACK_SIZE;
	mmu_frame_allocate(
		mmu_get_page(init->image.stack - KERNEL_STACK_SIZE, 0),
		MMU_FLAG_KERNEL);
	init->image.shm_heap = USER_SHM_LOW;

	init->flags         = PROC_FLAG_STARTED | PROC_FLAG_RUNNING;
	init->wait_queue    = list_create("process wait queue (init)", init);
	init->shm_mappings  = list_create("process shm mapping (init)", init);

	init->sched_node.prev = NULL;
	init->sched_node.next = NULL;
	init->sched_node.value = init;

	init->sleep_node.prev = NULL;
	init->sleep_node.next = NULL;
	init->sleep_node.value = init;

	init->timed_sleep_node = NULL;

	init->thread.page_directory = malloc(sizeof(page_directory_t));
	init->thread.page_directory->refcount = 1;
	init->thread.page_directory->directory = this_core->current_pml;
	spin_init(init->thread.page_directory->lock);
	init->description = strdup("[init]");
	list_insert(process_list, (void*)init);

	return init;
}

process_t * spawn_process(volatile process_t * parent, int flags) {
	process_t * proc = calloc(1,sizeof(process_t));

	proc->id          = get_next_pid();
	proc->group       = proc->id;
	proc->name        = strdup(parent->name);
	proc->description = NULL;
	proc->cmdline     = parent->cmdline; /* FIXME dup it? */

	proc->user        = parent->user;
	proc->real_user   = parent->real_user;
	proc->user_group  = parent->user_group;
	proc->real_user_group = parent->real_user_group;
	proc->mask        = parent->mask;
	proc->job         = parent->job;
	proc->session     = parent->session;

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
	memcpy((void*)proc->thread.fp_regs, (void*)parent->thread.fp_regs, 512);

	/* Entry is only stored for reference. */
	proc->image.entry       = parent->image.entry;
	proc->image.heap        = parent->image.heap;
	proc->image.stack       = (uintptr_t)valloc(KERNEL_STACK_SIZE) + KERNEL_STACK_SIZE;
	mmu_frame_allocate(
		mmu_get_page(proc->image.stack - KERNEL_STACK_SIZE, 0),
		MMU_FLAG_KERNEL);
	proc->image.shm_heap    = USER_SHM_LOW;

	if (flags & PROC_REUSE_FDS) {
		spin_lock(parent->fds->lock);
		proc->fds = parent->fds;
		proc->fds->refs++;
		spin_unlock(parent->fds->lock);
	} else {
		proc->fds = malloc(sizeof(fd_table_t));
		spin_init(proc->fds->lock);
		proc->fds->refs = 1;
		spin_lock(parent->fds->lock);
		proc->fds->length = parent->fds->length;
		proc->fds->capacity = parent->fds->capacity;
		proc->fds->entries = malloc(proc->fds->capacity * sizeof(fs_node_t *));
		proc->fds->modes   = malloc(proc->fds->capacity * sizeof(int));
		proc->fds->offsets = malloc(proc->fds->capacity * sizeof(uint64_t));
		for (uint32_t i = 0; i < parent->fds->length; ++i) {
			proc->fds->entries[i] = clone_fs(parent->fds->entries[i]);
			proc->fds->modes[i]   = parent->fds->modes[i];
			proc->fds->offsets[i] = parent->fds->offsets[i];
		}
		spin_unlock(parent->fds->lock);
	}

	proc->wd_node = clone_fs(parent->wd_node);
	proc->wd_name = strdup(parent->wd_name);

	proc->wait_queue   = list_create("process wait queue",proc);
	proc->shm_mappings = list_create("process shm mappings",proc);

	proc->sched_node.value = proc;
	proc->sleep_node.value = proc;

	gettimeofday(&proc->start, NULL);
	tree_node_t * entry = tree_node_create(proc);
	proc->tree_entry = entry;

	spin_lock(tree_lock);
	tree_node_insert_child_node(process_tree, parent->tree_entry, entry);
	list_insert(process_list, (void*)proc);
	spin_unlock(tree_lock);
	return proc;
}

extern void tree_remove_reparent_root(tree_t * tree, tree_node_t * node);

void process_reap(process_t * proc) {
	if (proc->tracees) {
		while (proc->tracees->length) {
			free(list_pop(proc->tracees));
		}
		free(proc->tracees);
	}

	/* Unmark the stack bottom's fault detector */
	mmu_frame_allocate(
		mmu_get_page(proc->image.stack - KERNEL_STACK_SIZE, 0),
		MMU_FLAG_KERNEL | MMU_FLAG_WRITABLE);

	free((void *)(proc->image.stack - KERNEL_STACK_SIZE));
	process_release_directory(proc->thread.page_directory);

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

	tree_node_t * entry = proc->tree_entry;
	if (!entry) {
		printf("Tried to delete process with no tree entry?\n");
		return;
	}
	if (process_tree->root == entry) {
		printf("Tried to delete process init...\n");
		return;
	}

	spin_lock(tree_lock);
	int has_children = entry->children->length;
	tree_remove_reparent_root(process_tree, entry);
	list_delete(process_list, list_find(process_list, proc));
	spin_unlock(tree_lock);

	if (has_children) {
		/* Wake up init */
		process_t * init = process_tree->root->value;
		wakeup_queue(init->wait_queue);
	}

	// FIXME bitset_clear(&pid_set, proc->id);
	proc->tree_entry = NULL;

	shm_release_all(proc);
	free(proc->shm_mappings);

	if (proc->supplementary_group_list) {
		proc->supplementary_group_count = 0;
		free(proc->supplementary_group_list);
	}

	/* Is someone using this process? */
	for (int i = 0; i < processor_count; ++i) {
		if (i == this_core->cpu_id) continue;
		if (processor_local_data[i].previous_process == proc ||
		    processor_local_data[i].current_process == proc) {
			process_reap_later(proc);
			return;
		}
	}

	process_reap(proc);
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
void make_process_ready(volatile process_t * proc) {
	int sleep_lock_is_mine = sleep_lock.owner == (this_core->cpu_id + 1);
	if (!sleep_lock_is_mine) spin_lock(sleep_lock);
	if (proc->sleep_node.owner != NULL) {
		if (proc->sleep_node.owner == sleep_queue) {
			/* The sleep queue is slightly special... */
			if (proc->timed_sleep_node) {
				list_delete(sleep_queue, proc->timed_sleep_node);
				proc->sleep_node.owner = NULL;
				free(proc->timed_sleep_node->value);
			}
		} else {
			/* This was blocked on a semaphore we can interrupt. */
			__sync_or_and_fetch(&proc->flags, PROC_FLAG_SLEEP_INT);
			list_delete((list_t*)proc->sleep_node.owner, (node_t*)&proc->sleep_node);
		}
	}
	if (!sleep_lock_is_mine) spin_unlock(sleep_lock);

	spin_lock(process_queue_lock);
	if (proc->sched_node.owner) {
		/* There's only one ready queue, so this means the process was already ready, which
		 * is indicative of a bug somewhere as we shouldn't be added processes to the ready
		 * queue multiple times. */
		spin_unlock(process_queue_lock);
		return;
	}

	list_append(process_queue, (node_t*)&proc->sched_node);
	spin_unlock(process_queue_lock);

	arch_wakeup_others();
}

/**
 * @brief Pop the next available process from the queue.
 *
 * Gets the next available process from the round-robin scheduling
 * queue. If there is no process to run, the idle task is returned.
 *
 * TODO This needs more locking for SMP...
 */
volatile process_t * next_ready_process(void) {
	spin_lock(process_queue_lock);

	if (!process_queue->head) {
		if (process_queue->length) {
			arch_fatal_prepare();
			printf("Queue has a length but head is NULL\n");
			arch_dump_traceback();
			arch_fatal();
		}
		spin_unlock(process_queue_lock);
		return this_core->kernel_idle_task;
	}

	node_t * np = list_dequeue(process_queue);

	if ((uintptr_t)np < 0xFFFFff0000000000UL || (uintptr_t)np > 0xFFFFfff000000000UL) {
		arch_fatal_prepare();
		printf("Suspicious pointer in queue: %#zx\n", (uintptr_t)np);
		arch_dump_traceback();
		arch_fatal();
	}
	volatile process_t * next = np->value;

	if ((next->flags & PROC_FLAG_RUNNING) && (next->owner != this_core->cpu_id)) {
		/* We pulled a process too soon, switch to idle for a bit so the
		 * core that marked this process as ready can finish switching away from it. */
		list_append(process_queue, (node_t*)&next->sched_node);
		spin_unlock(process_queue_lock);
		return this_core->kernel_idle_task;
	}

	spin_unlock(process_queue_lock);

	if (!(next->flags & PROC_FLAG_FINISHED)) {
		__sync_or_and_fetch(&next->flags, PROC_FLAG_RUNNING);
	}

	next->owner = this_core->cpu_id;

	return next;
}

/**
 * @brief Signal a semaphore.
 *
 * Okay, so toaru32 used these general-purpose lists of processes
 * as a sort of sempahore system, so often when you see 'queue' it
 * can be read as 'semaphore' and be equally valid (outside of the
 * 'ready queue', I guess). This will awaken all processes currently
 * in the semaphore @p queue, unless they were marked as finished in
 * which case they will be discarded.
 *
 * Note that these "semaphore queues" are binary semaphores - simple
 * locks, but with smarter logic than the "spin_lock" primitive also
 * used throughout the kernel, as that just blindly switches tasks
 * until its atomic swap succeeds.
 *
 * @param queue The semaphore to signal
 * @returns the number of processes successfully awoken
 */
int wakeup_queue(list_t * queue) {
	int awoken_processes = 0;
	spin_lock(wait_lock_tmp);
	while (queue->length > 0) {
		node_t * node = list_pop(queue);
		spin_unlock(wait_lock_tmp);
		if (!(((process_t *)node->value)->flags & PROC_FLAG_FINISHED)) {
			make_process_ready(node->value);
		}
		spin_lock(wait_lock_tmp);
		awoken_processes++;
	}
	spin_unlock(wait_lock_tmp);
	return awoken_processes;
}

/**
 * @brief Signal a semaphore, exceptionally.
 *
 * Wake up everything in the semaphore @p queue but mark every
 * waiter as having been interrupted, rather than gracefully awoken.
 * Generally that means the event they were waiting for did not
 * happen and may never happen.
 *
 * Otherwise, same semantics as @ref wakeup_queue.
 */
int wakeup_queue_interrupted(list_t * queue) {
	int awoken_processes = 0;
	spin_lock(wait_lock_tmp);
	while (queue->length > 0) {
		node_t * node = list_pop(queue);
		spin_unlock(wait_lock_tmp);
		if (!(((process_t *)node->value)->flags & PROC_FLAG_FINISHED)) {
			process_t * proc = node->value;
			__sync_or_and_fetch(&proc->flags, PROC_FLAG_SLEEP_INT);
			make_process_ready(proc);
		}
		spin_lock(wait_lock_tmp);
		awoken_processes++;
	}
	spin_unlock(wait_lock_tmp);
	return awoken_processes;
}

int wakeup_queue_one(list_t * queue) {
	int awoken_processes = 0;
	spin_lock(wait_lock_tmp);
	if (queue->length > 0) {
		node_t * node = list_pop(queue);
		spin_unlock(wait_lock_tmp);
		if (!(((process_t *)node->value)->flags & PROC_FLAG_FINISHED)) {
			make_process_ready(node->value);
		}
		spin_lock(wait_lock_tmp);
		awoken_processes++;
	}
	spin_unlock(wait_lock_tmp);
	return awoken_processes;
}

/**
 * @brief Wait for a binary semaphore.
 *
 * Wait for an event with everyone else in @p queue.
 *
 * @returns 1 if the wait was interrupted (eg. the event did not occur); 0 otherwise.
 */
int sleep_on(list_t * queue) {
	if (this_core->current_process->sleep_node.owner) {
		switch_task(0);
		return 0;
	}
	__sync_and_and_fetch(&this_core->current_process->flags, ~(PROC_FLAG_SLEEP_INT));
	spin_lock(wait_lock_tmp);
	list_append(queue, (node_t*)&this_core->current_process->sleep_node);
	spin_unlock(wait_lock_tmp);
	switch_task(0);
	return !!(this_core->current_process->flags & PROC_FLAG_SLEEP_INT);
}

int sleep_on_unlocking(list_t * queue, spin_lock_t * release) {
	__sync_and_and_fetch(&this_core->current_process->flags, ~(PROC_FLAG_SLEEP_INT));
	spin_lock(wait_lock_tmp);
	list_append(queue, (node_t*)&this_core->current_process->sleep_node);
	spin_unlock(wait_lock_tmp);

	spin_unlock(*release);

	switch_task(0);
	return !!(this_core->current_process->flags & PROC_FLAG_SLEEP_INT);
}

/**
 * @brief Indicates whether a process is ready to be run but not currently running.
 */
int process_is_ready(process_t * proc) {
	return (proc->sched_node.owner != NULL && !(proc->flags & PROC_FLAG_RUNNING));
}

int process_alert_node_locked(process_t * process, void * value);

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
	if (sleep_queue->length) {
		sleeper_t * proc = ((sleeper_t *)sleep_queue->head->value);
		while (proc && (proc->end_tick < seconds || (proc->end_tick == seconds && proc->end_subtick <= subseconds))) {

			if (proc->is_fswait) {
				proc->is_fswait = -1;
				process_alert_node_locked(proc->process,proc);
			} else {
				process_t * process = proc->process;
				process->sleep_node.owner = NULL;
				process->timed_sleep_node = NULL;
				if (!process_is_ready(process)) {
					make_process_ready(process);
				}
			}
			free(proc);
			free(list_dequeue(sleep_queue));
			if (sleep_queue->length) {
				proc = ((sleeper_t *)sleep_queue->head->value);
			} else {
				break;
			}
		}
	}
	spin_unlock(sleep_lock);
}

/**
 * @brief Wait until a given time.
 *
 * Suspends the current process until the given time. The process may
 * still be resumed by a signal or other mechanism, in which case the
 * sleep will not be resumed by the kernel.
 */
void sleep_until(process_t * process, unsigned long seconds, unsigned long subseconds) {
	spin_lock(sleep_lock);
	if (this_core->current_process->sleep_node.owner) {
		spin_unlock(sleep_lock);
		/* Can't sleep, sleeping already */
		return;
	}
	process->sleep_node.owner = sleep_queue;

	node_t * before = NULL;
	foreach(node, sleep_queue) {
		sleeper_t * candidate = ((sleeper_t *)node->value);
		if (!candidate) {
			printf("null candidate?\n");
			continue;
		}
		if (candidate->end_tick > seconds || (candidate->end_tick == seconds && candidate->end_subtick > subseconds)) {
			break;
		}
		before = node;
	}
	sleeper_t * proc = malloc(sizeof(sleeper_t));
	proc->process     = process;
	proc->end_tick    = seconds;
	proc->end_subtick = subseconds;
	proc->is_fswait = 0;
	process->timed_sleep_node = list_insert_after(sleep_queue, before, proc);
	spin_unlock(sleep_lock);
}

uint8_t process_compare(void * proc_v, void * pid_v) {
	pid_t pid = (*(pid_t *)pid_v);
	process_t * proc = (process_t *)proc_v;

	return (uint8_t)(proc->id == pid);
}

process_t * process_from_pid(pid_t pid) {
	if (pid < 0) return NULL;

	spin_lock(tree_lock);
	tree_node_t * entry = tree_find(process_tree,&pid,process_compare);
	spin_unlock(tree_lock);
	if (entry) {
		return (process_t *)entry->value;
	}
	return NULL;
}


long process_move_fd(process_t * proc, long src, long dest) {
	if ((size_t)src >= proc->fds->length || (dest != -1 && (size_t)dest >= proc->fds->length)) {
		return -1;
	}
	if (dest == -1) {
		dest = process_append_fd(proc, NULL);
	}
	if (proc->fds->entries[dest] != proc->fds->entries[src]) {
		close_fs(proc->fds->entries[dest]);
		proc->fds->entries[dest] = proc->fds->entries[src];
		proc->fds->modes[dest] = proc->fds->modes[src];
		proc->fds->offsets[dest] = proc->fds->offsets[src];
		open_fs(proc->fds->entries[dest], 0);
	}
	return dest;
}

void tasking_start(void) {
	this_core->current_process = spawn_init();
	this_core->kernel_idle_task = spawn_kidle(1);
}

static int wait_candidate(volatile process_t * parent, int pid, int options, volatile process_t * proc) {
	if (!proc) return 0;

	if (options & WNOKERN) {
		/* Skip kernel processes */
		if (proc->flags & PROC_FLAG_IS_TASKLET) return 0;
	}

	if (pid < -1) {
		if (proc->job == -pid || proc->id == -pid) return 1;
	} else if (pid == 0) {
		/* Matches our group ID */
		if (proc->job == parent->id) return 1;
	} else if (pid > 0) {
		/* Specific pid */
		if (proc->id == pid) return 1;
	} else {
		return 1;
	}
	return 0;
}

int waitpid(int pid, int * status, int options) {
	volatile process_t * volatile proc = (process_t*)this_core->current_process;
	#if 0
	if (proc->group) {
		proc = process_from_pid(proc->group);
	}
	#endif

	do {
		volatile process_t * candidate = NULL;
		int has_children = 0;
		int is_parent = 0;

		spin_lock(proc->wait_lock);

		/* First, find out if there is anyone to reap */
		foreach(node, proc->tree_entry->children) {
			if (!node->value) {
				continue;
			}
			volatile process_t * volatile child = ((tree_node_t *)node->value)->value;

			if (wait_candidate(proc, pid, options, child)) {
				has_children = 1;
				is_parent = 1;
				if (child->flags & PROC_FLAG_FINISHED) {
					candidate = child;
					break;
				}
				if ((child->flags & PROC_FLAG_SUSPENDED) && ((child->status & 0xFF) == 0x7F)) {
					int reason = (child->status >> 16) & 0xFF;
					if ((options & WSTOPPED) || (reason == 0xFF && (options & WUNTRACED))) {
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
			spin_unlock(proc->wait_lock);
			if (status) {
				*status = candidate->status;
			}
			candidate->status &= ~0xFF;
			int pid = candidate->id;
			if (is_parent && (candidate->flags & PROC_FLAG_FINISHED)) {
				while (*((volatile int *)&candidate->flags) & PROC_FLAG_RUNNING);
				proc->time_children += candidate->time_children + candidate->time_total;
				proc->time_sys_children += candidate->time_sys_children + candidate->time_sys;
				process_delete((process_t*)candidate);
			}
			return pid;
		} else {
			if (options & WNOHANG) {
				spin_unlock(proc->wait_lock);
				return 0;
			}
			/* Wait */
			if (sleep_on_unlocking(proc->wait_queue, &proc->wait_lock) != 0) {
				return -EINTR;
			}
		}
	} while (1);
}

int process_timeout_sleep(process_t * process, int timeout) {
	unsigned long s, ss;
	relative_time(0, timeout * 1000, &s, &ss);

	node_t * before = NULL;
	foreach(node, sleep_queue) {
		sleeper_t * candidate = ((sleeper_t *)node->value);
		if (candidate->end_tick > s || (candidate->end_tick == s && candidate->end_subtick > ss)) {
			break;
		}
		before = node;
	}
	sleeper_t * proc = malloc(sizeof(sleeper_t));
	proc->process     = process;
	proc->end_tick    = s;
	proc->end_subtick = ss;
	proc->is_fswait = 1;
	list_insert(((process_t *)process)->node_waits, proc);
	process->timeout_node = list_insert_after(sleep_queue, before, proc);

	return 0;
}

int process_wait_nodes(process_t * process,fs_node_t * nodes[], int timeout) {
	fs_node_t ** n = nodes;
	int index = 0;
	if (*n) {
		do {
			int result = selectcheck_fs(*n);
			if (result < 0) {
				return -EBADF;
			}
			if (result == 0) {
				return index;
			}
			n++;
			index++;
		} while (*n);
	}

	if (timeout == 0) {
		return index;
	}

	n = nodes;

	spin_lock(sleep_lock);
	spin_lock(process->sched_lock);
	process->node_waits = list_create("process fswaiters",process);
	if (*n) {
		do {
			if (selectwait_fs(*n, process) < 0) {
				printf("bad selectwait?\n");
			}
			n++;
		} while (*n);
	}

	if (timeout > 0) {
		process_timeout_sleep(process, timeout);
	} else {
		process->timeout_node = NULL;
	}

	process->awoken_index = -1;
	spin_unlock(process->sched_lock);
	spin_unlock(sleep_lock);

	/* Wait. */
	switch_task(0);

	return process->awoken_index;
}

int process_awaken_from_fswait(process_t * process, int index) {
	must_have_lock(sleep_lock);

	process->awoken_index = index;
	list_free(process->node_waits);
	free(process->node_waits);
	process->node_waits = NULL;

	if (process->timeout_node && process->timeout_node->owner == sleep_queue) {
		sleeper_t * proc = process->timeout_node->value;
		if (proc->is_fswait != -1) {
			list_delete(sleep_queue, process->timeout_node);
			free(process->timeout_node->value);
			free(process->timeout_node);
		}
	}
	process->timeout_node = NULL;

	make_process_ready(process);
	spin_unlock(process->sched_lock);
	return 0;
}

void process_awaken_signal(process_t * process) {
	spin_lock(sleep_lock);
	spin_lock(process->sched_lock);
	if (process->node_waits) {
		process_awaken_from_fswait(process, -EINTR);
	} else {
		spin_unlock(process->sched_lock);
	}
	spin_unlock(sleep_lock);
}

int process_alert_node_locked(process_t * process, void * value) {
	must_have_lock(sleep_lock);

	if (!is_valid_process(process)) {
		dprintf("core %d (pid=%d %s) attempted to alert invalid process %#zx\n",
			this_core->cpu_id, this_core->current_process->id, this_core->current_process->name,
			(uintptr_t)process);
		return 0;
	}

	spin_lock(process->sched_lock);

	if (!process->node_waits) {
		spin_unlock(process->sched_lock);
		return 0; /* Possibly already returned. Wait for another call. */
	}

	int index = 0;
	foreach(node, process->node_waits) {
		if (value == node->value) {
			return process_awaken_from_fswait(process, index);
		}
		index++;
	}

	spin_unlock(process->sched_lock);
	return -1;
}

int process_alert_node(process_t * process, void * value) {
	spin_lock(sleep_lock);
	int result = process_alert_node_locked(process, value);
	spin_unlock(sleep_lock);
	return result;
}

process_t * process_get_parent(process_t * process) {
	process_t * result = NULL;
	spin_lock(tree_lock);

	tree_node_t * entry = process->tree_entry;

	if (entry->parent) {
		result = entry->parent->value;
	}

	spin_unlock(tree_lock);
	return result;
}

void task_exit(int retval) {
	this_core->current_process->status = retval;

	/* free whatever we can */
	list_free(this_core->current_process->wait_queue);
	free(this_core->current_process->wait_queue);
	free(this_core->current_process->wd_name);
	if (this_core->current_process->node_waits) {
		list_free(this_core->current_process->node_waits);
		free(this_core->current_process->node_waits);
		this_core->current_process->node_waits = NULL;
	}

	if (this_core->current_process->fds) {
		spin_lock(this_core->current_process->fds->lock);
		this_core->current_process->fds->refs--;
		if (this_core->current_process->fds->refs == 0) {
			for (uint32_t i = 0; i < this_core->current_process->fds->length; ++i) {
				if (this_core->current_process->fds->entries[i]) {
					close_fs(this_core->current_process->fds->entries[i]);
					this_core->current_process->fds->entries[i] = NULL;
				}
			}
			free(this_core->current_process->fds->entries);
			free(this_core->current_process->fds->offsets);
			free(this_core->current_process->fds->modes);
			free(this_core->current_process->fds);
			this_core->current_process->fds = NULL;
		} else {
			spin_unlock(this_core->current_process->fds->lock);
		}
	}

	if (this_core->current_process->tracees) {
		spin_lock(this_core->current_process->wait_lock);
		while (this_core->current_process->tracees->length) {
			node_t * n = list_pop(this_core->current_process->tracees);
			process_t * tracee = n->value;
			free(n);
			if (is_valid_process(tracee)) {
				tracee->tracer = 0;
				__sync_and_and_fetch(&tracee->flags, ~(PROC_FLAG_TRACE_SIGNALS | PROC_FLAG_TRACE_SYSCALLS));
				if (tracee->flags & PROC_FLAG_SUSPENDED) {
					tracee->status = 0;
					__sync_and_and_fetch(&tracee->flags, ~(PROC_FLAG_SUSPENDED));
					make_process_ready(tracee);
				}
			}
		}
		spin_unlock(this_core->current_process->wait_lock);
	}

	update_process_times(1);

	process_t * parent = process_get_parent((process_t *)this_core->current_process);
	__sync_or_and_fetch(&this_core->current_process->flags, PROC_FLAG_FINISHED);

	if (this_core->current_process->tracer) {
		process_t * tracer = process_from_pid(this_core->current_process->tracer);
		if (tracer && tracer != parent) {
			spin_lock(tracer->wait_lock);
			wakeup_queue(tracer->wait_queue);
			spin_unlock(tracer->wait_lock);
		}
	}

	if (parent && !(parent->flags & PROC_FLAG_FINISHED)) {
		spin_lock(parent->wait_lock);
		send_signal(parent->group, SIGCHLD, 1);
		wakeup_queue(parent->wait_queue);
		spin_unlock(parent->wait_lock);
	}

	switch_next();
}

#define PUSH(stack, type, item) stack -= sizeof(type); \
							*((volatile type *) stack) = item

pid_t fork(void) {
	uintptr_t sp, bp;
	process_t * parent = (process_t*)this_core->current_process;
	union PML * directory = mmu_clone(parent->thread.page_directory->directory);
	process_t * new_proc = spawn_process(parent, 0);
	new_proc->thread.page_directory = malloc(sizeof(page_directory_t));
	new_proc->thread.page_directory->refcount = 1;
	new_proc->thread.page_directory->directory = directory;
	spin_init(new_proc->thread.page_directory->lock);

	memcpy(new_proc->signals, parent->signals, sizeof(struct signal_config) * (NUMSIGNALS+1));
	new_proc->blocked_signals = parent->blocked_signals;

	struct regs r;
	memcpy(&r, parent->syscall_registers, sizeof(struct regs));
	sp = new_proc->image.stack;
	bp = sp;

	arch_syscall_return(&r, 0);
	PUSH(sp, struct regs, r);

	new_proc->syscall_registers = (void*)sp;
	new_proc->thread.context.sp = sp;
	new_proc->thread.context.bp = bp;
	new_proc->thread.context.tls_base = parent->thread.context.tls_base;
	new_proc->thread.context.ip = (uintptr_t)&arch_resume_user;
	arch_save_context(&parent->thread);
	memcpy(new_proc->thread.context.saved, parent->thread.context.saved, sizeof(parent->thread.context.saved));

	#if 0
	printf("fork(): resuming with register context\n");
	extern void aarch64_regs(struct regs *);
	aarch64_regs(&r);
	printf("fork(): and arch context:\n");
	extern void aarch64_context(process_t * proc);
	aarch64_context(new_proc);
	#endif

	if (parent->flags & PROC_FLAG_IS_TASKLET) new_proc->flags |= PROC_FLAG_IS_TASKLET;
	make_process_ready(new_proc);
	return new_proc->id;
}

pid_t clone(uintptr_t new_stack, uintptr_t thread_func, uintptr_t arg) {
	uintptr_t sp, bp;
	process_t * parent = (process_t *)this_core->current_process;
	process_t * new_proc = spawn_process(this_core->current_process, 1);
	new_proc->thread.page_directory = this_core->current_process->thread.page_directory;
	spin_lock(new_proc->thread.page_directory->lock);
	new_proc->thread.page_directory->refcount++;
	spin_unlock(new_proc->thread.page_directory->lock);
	memcpy(new_proc->signals, parent->signals, sizeof(struct signal_config) * (NUMSIGNALS+1));
	new_proc->blocked_signals = parent->blocked_signals;

	struct regs r;
	memcpy(&r, parent->syscall_registers, sizeof(struct regs));
	sp = new_proc->image.stack;
	bp = sp;

	/* Set the gid */
	if (this_core->current_process->group) {
		new_proc->group = this_core->current_process->group;
	} else {
		/* We are the session leader */
		new_proc->group = this_core->current_process->id;
	}

	/* different calling convention */
	#if defined(__x86_64__)
	r.rdi = arg;
	PUSH(new_stack, uintptr_t, (uintptr_t)0);
	#elif defined(__aarch64__)
	r.x0 = arg;
	r.x30 = 0;
	#endif
	PUSH(sp, struct regs, r);
	new_proc->syscall_registers = (void*)sp;
	#if defined(__x86_64__)
	new_proc->syscall_registers->rsp = new_stack;
	new_proc->syscall_registers->rbp = new_stack;
	new_proc->syscall_registers->rip = thread_func;
	#elif defined(__aarch64__)
	new_proc->syscall_registers->user_sp = new_stack;
	new_proc->syscall_registers->x29 = new_stack;
	new_proc->thread.context.saved[10] = thread_func;
	#endif
	new_proc->thread.context.sp = sp;
	new_proc->thread.context.bp = bp;
	new_proc->thread.context.tls_base = this_core->current_process->thread.context.tls_base;
	new_proc->thread.context.ip = (uintptr_t)&arch_resume_user;
	if (parent->flags & PROC_FLAG_IS_TASKLET) new_proc->flags |= PROC_FLAG_IS_TASKLET;
	make_process_ready(new_proc);
	return new_proc->id;
}

process_t * spawn_worker_thread(void (*entrypoint)(void * argp), const char * name, void * argp) {
	process_t * proc = calloc(1,sizeof(process_t));

	proc->flags = PROC_FLAG_IS_TASKLET | PROC_FLAG_STARTED;

	proc->id          = get_next_pid();
	proc->group       = proc->id;
	proc->name        = strdup(name);
	proc->description = NULL;
	proc->cmdline     = NULL;

	/* Are these necessary for tasklets? Should probably all be zero. */
	proc->user        = 0;
	proc->real_user   = 0;
	proc->user_group  = 0;
	proc->real_user_group = 0;
	proc->mask        = 0;
	proc->job         = proc->id;
	proc->session     = proc->id;

	proc->thread.page_directory = malloc(sizeof(page_directory_t));
	proc->thread.page_directory->refcount = 1;
	proc->thread.page_directory->directory = mmu_clone(mmu_get_kernel_directory());
	spin_init(proc->thread.page_directory->lock);

	proc->image.stack       = (uintptr_t)valloc(KERNEL_STACK_SIZE) + KERNEL_STACK_SIZE;
	PUSH(proc->image.stack, uintptr_t, (uintptr_t)entrypoint);
	PUSH(proc->image.stack, void*, argp);

	proc->thread.context.sp = proc->image.stack;
	proc->thread.context.bp = proc->image.stack;
	proc->thread.context.ip = (uintptr_t)&arch_enter_tasklet;


	proc->wait_queue   = list_create("worker thread wait queue",proc);
	proc->shm_mappings = list_create("worker thread shm mappings",proc);

	proc->sched_node.value = proc;
	proc->sleep_node.value = proc;

	gettimeofday(&proc->start, NULL);
	tree_node_t * entry = tree_node_create(proc);
	proc->tree_entry = entry;

	spin_lock(tree_lock);
	tree_node_insert_child_node(process_tree, this_core->current_process->tree_entry, entry);
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
