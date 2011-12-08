/* vim: tabstop=4 shiftwidth=4 noexpandtab
 */

#ifndef PROCESS_H
#define PROCESS_H

#include <system.h>
#include <tree.h>
#include <signal.h>

typedef signed int    pid_t;
typedef unsigned int  user_t;
typedef unsigned int  group_t;
typedef unsigned char status_t;

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
	group_t       group;        /* Effective group */
	thread_t      thread;       /* Associated task information */
	tree_node_t * tree_entry;   /* Process Tree Entry */
	image_t       image;        /* Binary image information */
	fs_node_t *   wd_node;      /* Working directory VFS node */
	char *        wd_name;      /* Working directory path name */
	fd_table_t    fds;          /* File descriptor table */
	status_t      status;       /* Process status */
	sig_table_t   signals;      /* Signal table */
} process_t;

void initialize_process_tree();
void debug_print_process_tree();

#endif
