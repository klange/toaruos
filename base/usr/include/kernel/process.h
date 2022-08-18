#pragma once

#include <stdint.h>
#include <kernel/types.h>
#include <kernel/vfs.h>
#include <kernel/tree.h>
#include <kernel/list.h>
#include <kernel/spinlock.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/signal_defs.h>
#include <sys/signal.h>

#ifdef __x86_64__
#include <kernel/arch/x86_64/pml.h>
#endif

#ifdef __aarch64__
#include <kernel/arch/aarch64/pml.h>
#endif


#define PROC_REUSE_FDS 0x0001
#define KERNEL_STACK_SIZE 0x9000
#define USER_ROOT_UID 0

typedef struct {
	intptr_t refcount;
	union PML * directory;
	spin_lock_t lock;
} page_directory_t;

typedef struct {
	uintptr_t sp;        /* 0 */
	uintptr_t bp;        /* 8 */
	uintptr_t ip;        /* 16 */
	uintptr_t tls_base;  /* 24 */
#ifdef __x86_64__
	uintptr_t saved[5]; /* XXX Arch dependent */
#elif defined(__aarch64__)
	uintptr_t saved[32];
#endif
	/**
	 * 32: rbx
	 * 40: r12
	 * 48: r13
	 * 56: r14
	 * 64: r15
	 */
} kthread_context_t;

typedef struct thread {
	kthread_context_t context;
	uint8_t fp_regs[512] __attribute__((aligned(16)));
	page_directory_t * page_directory;
} thread_t;

typedef struct image {
	uintptr_t entry;
	uintptr_t heap;
	uintptr_t stack;
	uintptr_t shm_heap;
	uintptr_t userstack;
	spin_lock_t lock;
} image_t;

typedef struct file_descriptors {
	fs_node_t ** entries;
	uint64_t * offsets;
	int * modes;
	size_t length;
	size_t capacity;
	size_t refs;
	spin_lock_t lock;
} fd_table_t;

struct signal_config {
	uintptr_t handler;
	sigset_t  mask;
	int flags;
};

#define PROC_FLAG_IS_TASKLET 0x01
#define PROC_FLAG_FINISHED   0x02
#define PROC_FLAG_STARTED    0x04
#define PROC_FLAG_RUNNING    0x08
#define PROC_FLAG_SLEEP_INT  0x10
#define PROC_FLAG_SUSPENDED  0x20

#define PROC_FLAG_TRACE_SYSCALLS     0x40
#define PROC_FLAG_TRACE_SIGNALS      0x80

typedef struct process {
	pid_t id;    /* PID */
	pid_t group; /* thread group */
	pid_t job;   /* tty job */
	pid_t session; /* tty session */
	int status; /* status code */
	unsigned int flags; /* finished, started, running, isTasklet */
	int owner;

	uid_t user;
	uid_t real_user;

	gid_t user_group;
	gid_t real_user_group;

	unsigned int mask;

	char * name;
	char * description;
	char ** cmdline;

	char * wd_name;
	fs_node_t * wd_node;
	fd_table_t *  fds;               /* File descriptor table */

	tree_node_t * tree_entry;
	struct regs * syscall_registers;
	struct regs * interrupt_registers;
	list_t * wait_queue;
	list_t * shm_mappings;
	list_t * node_waits;

	node_t sched_node;
	node_t sleep_node;
	node_t * timed_sleep_node;
	node_t * timeout_node;

	struct timeval start;
	int awoken_index;

	thread_t thread;
	image_t image;

	spin_lock_t sched_lock;

	struct signal_config signals[NUMSIGNALS+1];
	sigset_t blocked_signals;
	sigset_t pending_signals;
	sigset_t awaited_signals;

	int supplementary_group_count;
	gid_t * supplementary_group_list;

	/* Process times */
	uint64_t time_prev;         /* user time from previous update of usage[] */
	uint64_t time_total;        /* user time */
	uint64_t time_sys;          /* system time */
	uint64_t time_in;           /* tsc stamp of when this process last entered the running state */
	uint64_t time_switch;       /* tsc stamp of when this process last started doing system things */
	uint64_t time_children;     /* sum of user times from waited-for children */
	uint64_t time_sys_children; /* sum of sys times from waited-for children */
	uint16_t usage[4];          /* four permille samples over some period (currently 4Hz) */

	/* Tracing */
	pid_t tracer;
	spin_lock_t wait_lock;
	list_t * tracees;

	/* Syscall restarting */
	long interrupted_system_call;
} process_t;

typedef struct {
	uint64_t end_tick;
	uint64_t end_subtick;
	process_t * process;
	int is_fswait;
} sleeper_t;

struct ProcessorLocal {
	/**
	 * @brief The running process on this core.
	 *
	 * The current_process is a pointer to the process struct for
	 * the process, userspace-thread, or kernel tasklet currently
	 * executing. Once the scheduler is active, this should always
	 * be set. If a core is not currently doing, its current_process
	 * should be the core's idle task.
	 *
	 * Because a process's data can be modified by nested interrupt
	 * contexts, we mark them as volatile to avoid making assumptions
	 * based on register-stored cached values.
	 */
	volatile process_t * current_process;
	/**
	 * @brief Idle loop.
	 *
	 * This is a special kernel tasklet that sits in a loop
	 * waiting for an interrupt from a preemption source or hardware
	 * device. Its context should never be saved, it should never
	 * be added to a sleep queue, and it should be scheduled whenever
	 * there is nothing else to do.
	 */
	process_t * kernel_idle_task;
	/**
	 * @brief Process this core was last scheduled to run.
	 */
	volatile process_t * previous_process;

	int cpu_id;
	union PML * current_pml;

	struct regs * interrupt_registers;

#ifdef __x86_64__
	int lapic_id;
	/* Processor information loaded at startup. */
	int  cpu_model;
	int  cpu_family;
	char cpu_model_name[48];
	const char * cpu_manufacturer;
#endif

#ifdef __aarch64__
	uintptr_t sp_el1;
	uint64_t  midr;
#endif
};

extern struct ProcessorLocal processor_local_data[];
extern int processor_count;

/**
 * @brief Core-local kernel data.
 *
 * x86-64: Marking this as __seg_gs makes it %gs-base-relative.
 * aarch64: We shove this in x18 and ref off of that; -ffixed-x18 and don't forget to reload it from TPIDR_EL1
 */
#ifdef __x86_64__
static struct ProcessorLocal __seg_gs * const this_core = 0;
#else
register struct ProcessorLocal * this_core asm("x18");
#endif

extern unsigned long process_append_fd(process_t * proc, fs_node_t * node);
extern long process_move_fd(process_t * proc, long src, long dest);
extern void initialize_process_tree(void);
extern process_t * process_from_pid(pid_t pid);

extern void process_delete(process_t * proc);
extern void make_process_ready(volatile process_t * proc);
extern volatile process_t * next_ready_process(void);
extern int wakeup_queue(list_t * queue);
extern int wakeup_queue_interrupted(list_t * queue);
extern int sleep_on(list_t * queue);
extern int sleep_on_unlocking(list_t * queue, spin_lock_t * release);
extern int process_alert_node(process_t * process, void * value);
extern void sleep_until(process_t * process, unsigned long seconds, unsigned long subseconds);
extern void switch_task(uint8_t reschedule);
extern int process_wait_nodes(process_t * process,fs_node_t * nodes[], int timeout);
extern process_t * process_get_parent(process_t * process);
extern int process_is_ready(process_t * proc);
extern void wakeup_sleepers(unsigned long seconds, unsigned long subseconds);
extern void task_exit(int retval);
extern __attribute__((noreturn)) void switch_next(void);
extern int process_awaken_from_fswait(process_t * process, int index);
extern void process_awaken_signal(process_t * process);
extern void process_release_directory(page_directory_t * dir);
extern process_t * spawn_worker_thread(void (*entrypoint)(void * argp), const char * name, void * argp);
extern pid_t fork(void);
extern pid_t clone(uintptr_t new_stack, uintptr_t thread_func, uintptr_t arg);
extern int waitpid(int pid, int * status, int options);
extern int exec(const char * path, int argc, char *const argv[], char *const env[], int interp_depth);
extern void update_process_usage(uint64_t clock_ticks, uint64_t perf_scale);

extern tree_t * process_tree;  /* Parent->Children tree */
extern list_t * process_list;  /* Flat storage */
extern list_t * process_queue; /* Ready queue */
extern list_t * sleep_queue;

extern void arch_enter_tasklet(void);
extern __attribute__((noreturn)) void arch_resume_user(void);
extern __attribute__((noreturn)) void arch_restore_context(volatile thread_t * buf);
extern __attribute__((returns_twice)) int arch_save_context(volatile thread_t * buf);
extern void arch_restore_floating(process_t * proc);
extern void arch_save_floating(process_t * proc);
extern void arch_set_kernel_stack(uintptr_t);
extern void arch_enter_user(uintptr_t entrypoint, int argc, char * argv[], char * envp[], uintptr_t stack);
__attribute__((noreturn))
extern void arch_enter_signal_handler(uintptr_t,int,struct regs*);
extern void arch_wakeup_others(void);
extern int arch_return_from_signal_handler(struct regs *r);

