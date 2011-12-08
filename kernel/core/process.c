#include <system.h>
#include <process.h>
#include <tree.h>
#include <list.h>

tree_t * process_tree;
list_t * process_queue;


char * default_name = "[unnamed]";

void initialize_process_tree() {
	process_tree = tree_create();
	process_queue = list_create();
}

void debug_print_process_tree_node(tree_node_t * node, size_t height) {
	if (!node) return;
	for (uint32_t i = 0; i < height; ++i) { kprintf("  "); }
	process_t * proc = (process_t *)node->value;
	kprintf("[%d] %s", proc->id, proc->name);
	if (proc->description) {
		kprintf(" %s", proc->description);
	}
	kprintf("\n");
	foreach(child, node->children) {
		debug_print_process_tree_node(child->value, height + 1);
	}
}

void debug_print_process_tree() {
	debug_print_process_tree_node(process_tree->root, 0);
}

process_t * next_ready_process() {
	node_t * np = list_pop(process_queue);
	if (!np) {
		/* Ready queue is empty! */
		return NULL;
	}
	process_t * next = np->value;
	free(np);
	return next;
}

void make_process_ready(process_t * proc) {
	list_insert(process_queue, (void *)proc);
}

void delete_process(process_t * proc) {
	tree_node_t * entry = proc->tree_entry;
	assert(entry && "Attempted to remove a process without a ps-tree entry.");
	assert((entry != process_tree->root) && "Attempted to kill init.");
	tree_remove(process_tree, entry);
}

void process_destroy() {
	/* Free all the dynamicly allocate elements of a process */
}

process_t * debug_make_init() {
	if (process_tree->root) {
		return process_tree->root->value;
	}
	process_t * init = malloc(sizeof(process_t));
	tree_set_root(process_tree, (void *)init);
	init->tree_entry = process_tree->root;
	init->id    = 1;
	init->name  = "init";
	init->user  = 0;
	init->group = 0;
	init->fds.length   = 0;
	init->fds.capacity = 4;
	init->fds.entries  = malloc(sizeof(fs_node_t *) * init->fds.capacity);
	init->wd_node = fs_root;
	init->wd_name = "/";
	init->status  = 0;

	init->description = "Herp derp.";
	return init;
}

pid_t get_next_pid() {
	static pid_t next = 2;
	return (next++);
}

void process_disown(process_t * proc) {
	assert(process_tree->root && "No init, has the process tree been initialized?");
	tree_node_t * entry = proc->tree_entry;
	tree_break_off(process_tree, entry);
	tree_node_insert_child_node(process_tree, process_tree->root, entry);
}

process_t * spawn_process(process_t * parent) {
	process_t * proc = malloc(sizeof(process_t));
	proc->id = get_next_pid();
	proc->name = default_name;
	proc->description = NULL;
	tree_node_t * entry = tree_node_create(proc);
	proc->tree_entry = entry;
	tree_node_insert_child_node(process_tree, parent->tree_entry, entry);
	return proc;
}

uint8_t process_compare(void * proc_v, void * pid_v) {
	pid_t pid = (*(pid_t *)pid_v);
	process_t * proc = (process_t *)proc_v;

	return (uint8_t)(proc->id == pid);
}

process_t * process_from_pid(pid_t pid) {
	assert((pid > 0) && "Tried to retreive a process with PID < 0");
	tree_node_t * entry = tree_find(process_tree,&pid,process_compare);
	if (entry) {
		return (process_t *)entry->value;
	} else {
		return NULL;
	}
}

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
