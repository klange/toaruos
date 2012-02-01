/* vim: tabstop=4 shiftwidth=4 noexpandtab
 */

#ifndef PROCESS_H
#define PROCESS_H

#include <system.h>
#include <tree.h>
#include <signal.h>

#define KERNEL_STACK_SIZE 0x2000

typedef signed int    pid_t;
typedef unsigned int  user_t;
typedef unsigned int  group_t;
typedef unsigned char status_t;

#define USER_ROOT_UID (user_t)0

/* Unix waitpid() options */
enum wait_option{
	WCONTINUED,
	WNOHANG,
	WUNTRACED
};

/* x86 task */
typedef struct thread {
	uintptr_t  esp; /* Stack Pointer */
	uintptr_t  ebp; /* Base Pointer */
	uintptr_t  eip; /* Instruction Pointer */

	page_directory_t * page_directory; /* Page Directory */
} thread_t;

/* Portable image struct */
typedef struct image {
	size_t    size;        /* Image size */
	uintptr_t entry;       /* Binary entry point */
	uintptr_t heap;        /* Heap pointer */
	uintptr_t heap_actual; /* Actual heap location */
	uintptr_t stack;       /* Process kernel stack */
	uintptr_t user_stack;  /* User stack */
} image_t;

/* Resizable descriptor table */
typedef struct descriptor_table {
	fs_node_t ** entries;
	size_t       length;
	size_t       capacity;
} fd_table_t;

/* XXX */
#define SIG_COUNT 10

/* Signal Table */
typedef struct signal_table {
	uintptr_t *   functions[NUMSIGNALS];
} sig_table_t;

/* Portable process struct */
typedef struct process {
	pid_t         id;           /* Process ID (pid) */
	char *        name;         /* Process Name */
	char *        description;  /* Process description */
	user_t        user;         /* Effective user */
	group_t       group;        /* Process scheduling group */
	thread_t      thread;       /* Associated task information */
	tree_node_t * tree_entry;   /* Process Tree Entry */
	image_t       image;        /* Binary image information */
	fs_node_t *   wd_node;      /* Working directory VFS node */
	char *        wd_name;      /* Working directory path name */
	fd_table_t    fds;          /* File descriptor table */
	status_t      status;       /* Process status */
	sig_table_t   signals;      /* Signal table */
	uint8_t       finished;     /* Status indicator */
	struct regs * syscall_registers; /* Registers at interrupt */
	list_t *      wait_queue;
} process_t;

void initialize_process_tree();
process_t * spawn_process(volatile process_t * parent);
void debug_print_process_tree();
process_t * spawn_init();
void set_process_environment(process_t * proc, page_directory_t * directory);
void make_process_ready(process_t * proc);
void make_process_reapable(process_t * proc);
uint8_t process_available();
process_t * next_ready_process();
uint8_t should_reap();
process_t * next_reapable_process();
uint32_t process_append_fd(process_t * proc, fs_node_t * node);
process_t * process_from_pid(pid_t pid);
void delete_process(process_t * proc);
uint32_t process_move_fd(process_t * proc, int src, int dest);

volatile process_t * current_process;

#endif
