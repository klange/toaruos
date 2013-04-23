/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Processes
 *
 * Internal format format for a process and functions to spawn
 * new processes and manage the process tree.
 */
#include <system.h>
#include <process.h>
#include <tree.h>
#include <list.h>
#include <logging.h>
#include <shm.h>

tree_t * process_tree;  /* Parent->Children tree */
list_t * process_list;  /* Flat storage */
list_t * process_queue; /* Ready queue */
list_t * reap_queue;    /* Processes to reap */
list_t * sleep_queue;
list_t * recently_reaped;
volatile process_t * current_process = NULL;

static uint8_t volatile reap_lock;
static uint8_t volatile tree_lock;

/* Default process name string */
char * default_name = "[unnamed]";

/*
 * Initialize the process tree and ready queue.
 */
void initialize_process_tree() {
	process_tree = tree_create();
	process_list = list_create();
	process_queue = list_create();
	reap_queue = list_create();
	sleep_queue = list_create();
	recently_reaped = list_create();
}

/*
 * Recursively print a process node to the console.
 *
 * @param node   Node to print.
 * @param height Current depth in the tree.
 */
void debug_print_process_tree_node(tree_node_t * node, size_t height) {
	/* End recursion on a blank entry */
	if (!node) return;
	/* Indent output */
	for (uint32_t i = 0; i < height; ++i) { kprintf("  "); }
	/* Get the current process */
	process_t * proc = (process_t *)node->value;
	/* Print the process name */
	kprintf("%d.%d %s", proc->group ? proc->group : proc->id, proc->id, proc->name);
	if (proc->description) {
		/* And, if it has one, its description */
		kprintf(" %s", proc->description);
	}
	if (proc->finished) {
		kprintf(" [zombie]");
	}
	/* Linefeed */
	kprintf("\n");
	foreach(child, node->children) {
		/* Recursively print the children */
		debug_print_process_tree_node(child->value, height + 1);
	}
}

/*
 * Print the process tree to the console.
 */
void debug_print_process_tree() {
	debug_print_process_tree_node(process_tree->root, 0);
}

/*
 * Retreive the next ready process.
 * XXX: POPs from the ready queue!
 *
 * @return A pointer to the next process in the queue.
 */
process_t * next_ready_process() {
	node_t * np = list_dequeue(process_queue);
	assert(np && "Ready queue is empty.");
	process_t * next = np->value;
	return next;
}

process_t * next_reapable_process() {
	spin_lock(&reap_lock);
	node_t * np = list_dequeue(reap_queue);
	spin_unlock(&reap_lock);
	if (!np) { return NULL; }
	process_t * next = np->value;
	free(np);
	return next;
}

/*
 * Reinsert a process into the ready queue.
 *
 * @param proc Process to reinsert
 */
void make_process_ready(process_t * proc) {
	if (proc->sched_node.prev != NULL || proc->sched_node.next != NULL) /* Process is already ready, or someone stole our scheduling node. */ return;
	list_append(process_queue, &proc->sched_node);
}

void make_process_reapable(process_t * proc) {
	delete_process(proc);
	spin_lock(&reap_lock);
	list_insert(reap_queue, (void *)proc);
	spin_unlock(&reap_lock);
}

void set_reaped(process_t * proc) {
	spin_lock(&reap_lock);
	list_insert(recently_reaped, (void *)proc);
	spin_unlock(&reap_lock);
}

/*
 * Delete a process from the process tree
 *
 * @param proc Process to find and remove.
 */
void delete_process(process_t * proc) {
	tree_node_t * entry = proc->tree_entry;

	/* The process must exist in the tree, or the client is at fault */
	if (!entry) return;

	/* We can not remove the root, which is an error anyway */
	assert((entry != process_tree->root) && "Attempted to kill init.");

	/* Remove the entry. */
	spin_lock(&tree_lock);
	tree_remove(process_tree, entry);
	list_delete(process_list, list_find(process_list, proc));
	spin_unlock(&tree_lock);
}

/*
 * Spawn the initial process.
 *
 * @return A pointer to the new initial process entry
 */
process_t * spawn_init() {
	/* We can only do this once. */
	assert((!process_tree->root) && "Tried to regenerate init!");

	/* Allocate space for a new process */
	process_t * init = malloc(sizeof(process_t));
	/* Set it as the root process */
	tree_set_root(process_tree, (void *)init);
	/* Set its tree entry pointer so we can keep track
	 * of the process' entry in the process tree. */
	init->tree_entry = process_tree->root;
	init->id      = 0;       /* Init is PID 1 */
	init->group   = 0;
	init->name    = strdup("init");  /* Um, duh. */
	init->cmdline = NULL;
	init->user    = 0;       /* UID 0 */
	init->mask    = 022;     /* umask */
	init->group   = 0;       /* Task group 0 */
	init->status  = 0;       /* Run status */
	init->fds = malloc(sizeof(fd_table_t));
	init->fds->refs = 1;
	init->fds->length   = 3;  /* Initialize the file descriptors */
	init->fds->capacity = 4;
	init->fds->entries  = malloc(sizeof(fs_node_t *) * init->fds->capacity);

	/* Set the working directory */
	init->wd_node = clone_fs(fs_root);
	init->wd_name = strdup("/");

	/* Heap and stack pointers (and actuals) */
	init->image.entry       = 0;
	init->image.heap        = 0;
	init->image.heap_actual = 0;
	init->image.stack       = initial_esp + 1;
	init->image.user_stack  = 0;
	init->image.size        = 0;
	init->image.shm_heap    = SHM_START; /* Yeah, a bit of a hack. */

	/* Process is not finished */
	init->finished = 0;
	init->started = 1;
	init->wait_queue = list_create();
	init->shm_mappings = list_create();
	init->signal_queue = list_create();
	init->signal_kstack = NULL; /* None yet initialized */

	init->sched_node.prev = NULL;
	init->sched_node.next = NULL;
	init->sched_node.value = init;

	init->sleep_node.prev = NULL;
	init->sleep_node.next = NULL;
	init->sleep_node.value = init;

	/* What the hey, let's also set the description on this one */
	init->description = strdup("[init]");
	return init;
}

/*
 * Get the next available PID
 *
 * @return A usable PID for a new process.
 */
pid_t get_next_pid() {
	/* Terribly naïve, I know, but it works for now */
	static pid_t next = 1;
	return (next++);
}

/*
 * Disown a process from its parent.
 */
void process_disown(process_t * proc) {
	assert(process_tree->root && "No init, has the process tree been initialized?");

	/* Find the process in the tree */
	tree_node_t * entry = proc->tree_entry;
	/* Break it of from its current parent */
	spin_lock(&tree_lock);
	tree_break_off(process_tree, entry);
	/* And insert it back elsewhere */
	tree_node_insert_child_node(process_tree, process_tree->root, entry);
	spin_unlock(&tree_lock);
}

/*
 * Spawn a new process.
 *
 * @param parent The parent process to spawn the new one off of.
 * @return A pointer to the new process.
 */
process_t * spawn_process(volatile process_t * parent) {
	assert(process_tree->root && "Attempted to spawn a process without init.");

	/* Allocate a new process */
	debug_print(INFO,"   process_t {");
	process_t * proc = malloc(sizeof(process_t));
	debug_print(INFO,"   }");
	proc->id = get_next_pid(); /* Set its PID */
	proc->group = proc->id;    /* Set the GID */
	proc->name = strdup(default_name); /* Use the default name */
	proc->description = NULL;  /* No description */
	proc->cmdline = parent->cmdline;

	/* Copy permissions */
	proc->user  = parent->user;
	proc->mask = parent->mask;

	/* XXX this is wrong? */
	proc->group = parent->group;

	/* Zero out the ESP/EBP/EIP */
	proc->thread.esp = 0;
	proc->thread.ebp = 0;
	proc->thread.eip = 0;
	proc->thread.fpu_enabled = 0;

	/* Set the process image information from the parent */
	proc->image.entry       = parent->image.entry;
	proc->image.heap        = parent->image.heap;
	proc->image.heap_actual = parent->image.heap_actual;
	proc->image.size        = parent->image.size;
	debug_print(INFO,"    stack {");
	proc->image.stack       = (uintptr_t)malloc(KERNEL_STACK_SIZE) + KERNEL_STACK_SIZE;
	debug_print(INFO,"    }");
	proc->image.user_stack  = parent->image.user_stack;
	proc->image.shm_heap    = SHM_START; /* Yeah, a bit of a hack. */

	assert(proc->image.stack && "Failed to allocate kernel stack for new process.");

	/* Clone the file descriptors from the original process */
	proc->fds = malloc(sizeof(fd_table_t));
	proc->fds->refs     = 1;
	proc->fds->length   = parent->fds->length;
	proc->fds->capacity = parent->fds->capacity;
	debug_print(INFO,"    fds / files {");
	proc->fds->entries  = malloc(sizeof(fs_node_t *) * proc->fds->capacity);
	assert(proc->fds->entries && "Failed to allocate file descriptor table for new process.");
	debug_print(INFO,"    ---");
	for (uint32_t i = 0; i < parent->fds->length; ++i) {
		proc->fds->entries[i] = clone_fs(parent->fds->entries[i]);
	}
	debug_print(INFO,"    }");

	/* As well as the working directory */
	proc->wd_node = clone_fs(parent->wd_node);
	proc->wd_name = strdup(parent->wd_name);

	/* Zero out the process status */
	proc->status = 0;
	proc->finished = 0;
	proc->started = 0;
	memset(proc->signals.functions, 0x00, sizeof(uintptr_t) * NUMSIGNALS);
	proc->wait_queue = list_create();
	proc->shm_mappings = list_create();
	proc->signal_queue = list_create();
	proc->signal_kstack = NULL; /* None yet initialized */

	proc->sched_node.prev = NULL;
	proc->sched_node.next = NULL;
	proc->sched_node.value = proc;

	proc->sleep_node.prev = NULL;
	proc->sleep_node.next = NULL;
	proc->sleep_node.value = proc;

	/* Insert the process into the process tree as a child
	 * of the parent process. */
	tree_node_t * entry = tree_node_create(proc);
	assert(entry && "Failed to allocate a process tree node for new process.");
	proc->tree_entry = entry;
	spin_lock(&tree_lock);
	tree_node_insert_child_node(process_tree, parent->tree_entry, entry);
	list_insert(process_list, (void *)proc);
	spin_unlock(&tree_lock);

	/* Return the new process */
	return proc;
}

process_t * find_reaped_process(pid_t pid) {
	foreach(node, recently_reaped) {
		process_t * proc = node->value;
		if (proc && proc->id == pid) return proc;
	}
	return NULL;
}

uint8_t process_compare(void * proc_v, void * pid_v) {
	pid_t pid = (*(pid_t *)pid_v);
	process_t * proc = (process_t *)proc_v;

	return (uint8_t)(proc->id == pid);
}

process_t * process_from_pid(pid_t pid) {
	assert((pid > 0) && "Tried to retreive a process with PID < 0");

	spin_lock(&tree_lock);
	tree_node_t * entry = tree_find(process_tree,&pid,process_compare);
	spin_unlock(&tree_lock);
	if (entry) {
		return (process_t *)entry->value;
	} else {
		return find_reaped_process(pid);
	}
}

process_t * process_get_first_child_rec(tree_node_t * node, process_t * target) {
	if (!node) return NULL;
	process_t * proc = (process_t *)node->value;
	if (proc == target) {
		foreach(child, node->children) {
			process_t * cproc = (process_t *)((tree_node_t *)child->value)->value;
			return cproc;
		}
		return NULL;
	}
	foreach(child, node->children) {
		/* Recursively print the children */
		process_t * out = process_get_first_child_rec(child->value, target);
		if (out) return out;
	}
	return NULL;
}

process_t * process_get_first_child(process_t * process) {
	spin_lock(&tree_lock);
	process_t * result = process_get_first_child_rec(process_tree->root, process);
	spin_unlock(&tree_lock);
	return result;
}

/*
 * Wait for children.
 *
 * @param process Process doing the waiting.
 * @param pid     PID to wait for
 * @param status  [out] Where to put the status conditions of the waited-for process
 * @param options Options (unused)
 * @return A pointer to the process that broke the wait
 */
process_t * process_wait(process_t * process, pid_t pid, int * status, int options) {
	/* `options` is ignored */
	if (pid == -1) {
		/* wait for any child process */
	} else if (pid < 0) {
		/* wait for any porcess whose ->group == processes[abs(pid)]->group */
	} else if (pid == 0) {
		/* wait for any process whose ->group == process->group */
	} else {
		/* wait for processes[pid] */
	}
	return NULL;
}

/*
 * Wake up a sleeping process
 *
 * @param process Process to wake up
 * @param caller  Who woke it up
 * @return Don't know yet, but I think it should return something.
 */
int process_wake(process_t * process, process_t * caller) {

	return 0;
}

/*
 * Set the directory for a process.
 *
 * @param proc      Process to set the directory for.
 * @param directory Directory to set.
 */
void set_process_environment(process_t * proc, page_directory_t * directory) {
	assert(proc);
	assert(directory);

	proc->thread.page_directory = directory;
}

/*
 * Are there any processes available in the queue?
 * (Queue not empty)
 *
 * @return 1 if there are processes available, 0 otherwise
 */
uint8_t process_available() {
	return (process_queue->head != NULL);
}

uint8_t should_reap() {
	return (reap_queue->head != NULL);
}

/*
 * Append a file descriptor to a process.
 *
 * @param proc Process to append to
 * @param node The VFS node
 * @return The actual fd, for use in userspace
 */
uint32_t process_append_fd(process_t * proc, fs_node_t * node) {
	if (proc->fds->length == proc->fds->capacity) {
		proc->fds->capacity *= 2;
		proc->fds->entries = realloc(proc->fds->entries, sizeof(fs_node_t *) * proc->fds->capacity);
	}
	proc->fds->entries[proc->fds->length] = node;
	proc->fds->length++;
	return proc->fds->length-1;
}

/*
 * dup2() -> Move the file pointed to by `s(ou)rc(e)` into
 *           the slot pointed to be `dest(ination)`.
 *
 * @param proc  Process to do this for
 * @param src   Source file descriptor
 * @param dest  Destination file descriptor
 * @return The destination file descriptor, -1 on failure
 */
uint32_t process_move_fd(process_t * proc, int src, int dest) {
	if ((size_t)src > proc->fds->length || (size_t)dest > proc->fds->length) {
		return -1;
	}
#if 0
	if (proc->fds->entries[dest] != proc->fds->entries[src]) {
		close_fs(proc->fds->entries[src]);
	}
#endif
	proc->fds->entries[dest] = proc->fds->entries[src];
	return dest;
}

int wakeup_queue(list_t * queue) {
	int awoken_processes = 0;
	while (queue->length > 0) {
		node_t * node = list_pop(queue);
		if (!((process_t *)node->value)->finished) {
			make_process_ready(node->value);
		}
		awoken_processes++;
	}
	return awoken_processes;
}

int sleep_on(list_t * queue) {
	if (current_process->sleep_node.prev || current_process->sleep_node.next) {
		/* uh, we can't sleep right now, we're marked as ready */
		switch_task(0);
		return 0;
	}
	list_append(queue, (node_t *)&current_process->sleep_node);
	switch_task(0);
	return 0;
}

int process_is_ready(process_t * proc) {
	if (proc->sched_node.prev != NULL || proc->sched_node.next != NULL || process_queue->head == &proc->sched_node) return 1;
	return 0;
}

void wakeup_sleepers(unsigned long seconds, unsigned long subseconds) {
	if (sleep_queue->length) {
		sleeper_t * proc = ((sleeper_t *)sleep_queue->head->value);
		while (proc && (proc->end_tick < seconds || (proc->end_tick == seconds && proc->end_subtick <= subseconds))) {
			process_t * process = proc->process;
			if (!process_is_ready(process)) {
				make_process_ready(process);
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
}

void sleep_until(process_t * process, unsigned long seconds, unsigned long subseconds) {
	IRQ_OFF;
	debug_print(INFO, "Sleeping process %d until %d,%d", process->id, seconds, subseconds);
	node_t * before = NULL;
	foreach(node, sleep_queue) {
		sleeper_t * candidate = ((sleeper_t *)node->value);
		if (candidate->end_tick > seconds || (candidate->end_tick == seconds && candidate->end_subtick > subseconds)) {
			break;
		}
		before = node;
	}
	sleeper_t * proc = malloc(sizeof(sleeper_t));
	proc->process     = process;
	proc->end_tick    = seconds;
	proc->end_subtick = subseconds;
	list_insert_after(sleep_queue, before, proc);
	IRQ_RES;
}
