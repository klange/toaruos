/* vim: tabstop=4 shiftwidth=4 noexpandtab
 */
#ifndef __SYSTEM_H
#define __SYSTEM_H
#define _KERNEL_
#include <types.h>
#include <fs.h>
#include <va_list.h>
#include <list.h>
#include <task.h>
#include <process.h>

#define STR(x) #x
#define STRSTR(x) STR(x)

/* Binary Literals */
#define b(x) ((uint8_t)b_(0 ## x ## uL))
#define b_(x) ((x & 1) | (x >> 2 & 2) | (x >> 4 & 4) | (x >> 6 & 8) | (x >> 8 & 16) | (x >> 10 & 32) | (x >> 12 & 64) | (x >> 14 & 128))

/* Unimportant Kernel Strings */
#define KERNEL_UNAME "ToAruOS"
#define KERNEL_VERSION_STRING "0.0.1"

#define asm __asm__
#define volatile __volatile__

extern unsigned int __irq_sem;

#define IRQ_OFF { asm volatile ("cli"); }
#define IRQ_RES { asm volatile ("sti"); }
#define PAUSE   { asm volatile ("hlt"); }
#define IRQS_ON_AND_PAUSE { asm volatile ("sti\nhlt\ncli"); }

#define STOP while (1) { PAUSE; }

#define SYSCALL_VECTOR 0x7F
#define SIGNAL_RETURN 0xFFFFDEAF
#define THREAD_RETURN 0xFFFFB00F

extern void * code;
extern void * end;

extern char * boot_arg; /* Argument to pass to init */
extern char * boot_arg_extra; /* Extra data to pass to init */

extern void *sbrk(uintptr_t increment);

extern void tss_flush();

extern void spin_lock(uint8_t volatile * lock);
extern void spin_unlock(uint8_t volatile * lock);

/* Kernel Main */
extern int max(int,int);
extern int min(int,int);
extern int abs(int);
extern void swap(int *, int *);
extern void *memcpy(void *restrict dest, const void *restrict src, size_t count);
extern void *memmove(void *restrict dest, const void *restrict src, size_t count);
extern void *memset(void *dest, int val, size_t count);
extern unsigned short *memsetw(unsigned short *dest, unsigned short val, int count);
extern uint32_t strlen(const char *str);
extern char * strdup(const char *str);
extern char * strcpy(char * dest, const char * src);
extern int atoi(const char *str);
extern unsigned char inportb(unsigned short _port);
extern void outportb(unsigned short _port, unsigned char _data);
extern unsigned short inports(unsigned short _port);
extern void outports(unsigned short _port, unsigned short _data);
extern unsigned int inportl(unsigned short _port);
extern void outportl(unsigned short _port, unsigned int _data);
extern void outportsm(unsigned short port, unsigned char * data, unsigned long size);
extern void inportsm(unsigned short port, unsigned char * data, unsigned long size);
extern int strcmp(const char *a, const char *b);
extern char * strtok_r(char * str, const char * delim, char ** saveptr);
extern size_t lfind(const char * str, const char accept);
extern size_t rfind(const char * str, const char accept);
extern size_t strspn(const char * str, const char * accept);
extern char * strpbrk(const char * str, const char * accept);
extern uint32_t krand();
extern char * strstr(const char * haystack, const char * needle);
extern uint8_t startswith(const char * str, const char * accept);

/* VGA driver */
extern void cls();
extern void puts(char *str);
extern void settextcolor(unsigned char forecolor, unsigned char backcolor);
extern void resettextcolor();
extern void brighttextcolor();
extern void init_video();
extern void placech(unsigned char c, int x, int y, int attr);
extern void writechf(unsigned char c);
extern void writech(char c);
extern void place_csr(uint32_t x, uint32_t y);
extern void store_csr();
extern void restore_csr();
extern void set_serial(int);
extern void set_csr(int);

/* GDT */
extern void gdt_install();
extern void gdt_set_gate(int num, unsigned long base, unsigned long limit, unsigned char access,
			 unsigned char gran);
extern void set_kernel_stack(uintptr_t stack);

/* IDT */
extern void idt_install();
extern void idt_set_gate(unsigned char num, unsigned long base, unsigned short sel,
			 unsigned char flags);

/* Registers */
struct regs {
	unsigned int gs, fs, es, ds;
	unsigned int edi, esi, ebp, esp, ebx, edx, ecx, eax;
	unsigned int int_no, err_code;
	unsigned int eip, cs, eflags, useresp, ss;
};

typedef struct regs regs_t;

typedef void (*irq_handler_t) (struct regs *);

/* Panic */
#define HALT_AND_CATCH_FIRE(mesg, regs) halt_and_catch_fire(mesg, __FILE__, __LINE__, regs)
#define ASSERT(statement) ((statement) ? (void)0 : assert_failed(__FILE__, __LINE__, #statement))
#define assert(statement) ((statement) ? (void)0 : assert_failed(__FILE__, __LINE__, #statement))
void halt_and_catch_fire(char *error_message, const char *file, int line, struct regs * regs);
void assert_failed(const char *file, uint32_t line, const char *desc);

/* ISRS */
extern void isrs_install();
extern void isrs_install_handler(int isrs, irq_handler_t);
extern void isrs_uninstall_handler(int isrs);

/* Interrupt Handlers */
extern void irq_install();
extern void irq_install_handler(int irq, irq_handler_t);
extern void irq_uninstall_handler(int irq);
extern void irq_gates();
extern void irq_ack();

/* Timer */
extern void timer_install();
extern long timer_ticks;
extern void timer_wait(int ticks);

/* Keyboard */
extern void keyboard_install();
extern void keyboard_wait();
extern void putch(unsigned char c);

/* Mouse */
extern void mouse_install();

/* kprintf */
extern size_t vasprintf(char * buf, const char *fmt, va_list args);
#ifndef EXTREME_KPRINTF_DEBUGGING
extern int    kprintf(const char *fmt, ...);
#else
/* This is really, really extreme */
extern int    _kprintf(char * file, int line, const char *fmt, ...);
#define kprintf(...) _kprintf(__FILE__, __LINE__, __VA_ARGS__)
#endif

extern short  kprint_to_serial;
extern short  kprint_to_screen;
extern void * kprint_to_file;

extern int    sprintf(char *buf, const char *fmt, ...);
extern int    kgets(char *buf, int size);
typedef void (*kgets_redraw_t)();
extern kgets_redraw_t kgets_redraw_func;
typedef void (*kgets_tab_complete_t)(char *);
extern kgets_tab_complete_t kgets_tab_complete_func;
extern void kgets_redraw_buffer();
typedef void (*kgets_special_t)(char *);
extern kgets_special_t kgets_key_down;
extern kgets_special_t kgets_key_up;
extern kgets_special_t kgets_key_left;
extern kgets_special_t kgets_key_right;

/* Memory Management */
extern uintptr_t placement_pointer;
extern void kmalloc_startat(uintptr_t address);
extern uintptr_t kmalloc_real(size_t size, int align, uintptr_t * phys);
extern uintptr_t kmalloc(size_t size);
extern uintptr_t kvmalloc(size_t size);
extern uintptr_t kmalloc_p(size_t size, uintptr_t * phys);
extern uintptr_t kvmalloc_p(size_t size, uintptr_t * phys);

// Page types moved to task.h

page_directory_t *kernel_directory;
page_directory_t *current_directory;

extern void paging_install(uint32_t memsize);
extern void switch_page_directory(page_directory_t * new);
extern page_t *get_page(uintptr_t address, int make, page_directory_t * dir);
extern void page_fault(struct regs *r);
extern void dma_frame(page_t * page, int, int, uintptr_t);
extern void debug_print_directory();

void heap_install();

void alloc_frame(page_t *page, int is_kernel, int is_writeable);
void free_frame(page_t *page);
uintptr_t memory_use();
uintptr_t memory_total();

/* klmalloc */
void * __attribute__ ((malloc)) malloc(size_t size);
void * __attribute__ ((malloc)) realloc(void *ptr, size_t size);
void * __attribute__ ((malloc)) calloc(size_t nmemb, size_t size);
void * __attribute__ ((malloc)) valloc(size_t size);
void free(void *ptr);

/* shell */
extern void start_shell();

/* Serial */
#define SERIAL_PORT_A 0x3F8
#define SERIAL_PORT_B 0x2F8
#define SERIAL_PORT_C 0x3E8
#define SERIAL_PORT_D 0x2E8

#define SERIAL_IRQ 4

extern void serial_install();
extern int serial_rcvd(int device);
extern char serial_recv(int device);
extern char serial_recv_async(int device);
extern void serial_send(int device, char out);
extern void serial_string(int device, char * out);

/* Tasks */
extern uintptr_t read_eip();
extern void copy_page_physical(uint32_t, uint32_t);
extern page_directory_t * clone_directory(page_directory_t * src);
extern page_table_t * clone_table(page_table_t * src, uintptr_t * physAddr);
extern void move_stack(void *new_stack_start, size_t size);
extern void kexit(int retval);
extern void task_exit(int retval);
extern uint32_t next_pid;

typedef struct tss_entry {
	uint32_t	prev_tss;
	uint32_t	esp0;
	uint32_t	ss0;
	uint32_t	esp1;
	uint32_t	ss1;
	uint32_t	esp2;
	uint32_t	ss2;
	uint32_t	cr3;
	uint32_t	eip;
	uint32_t	eflags;
	uint32_t	eax;
	uint32_t	ecx;
	uint32_t	edx;
	uint32_t	ebx;
	uint32_t	esp;
	uint32_t	ebp;
	uint32_t	esi;
	uint32_t	edi;
	uint32_t	es;
	uint32_t	cs;
	uint32_t	ss;
	uint32_t	ds;
	uint32_t	fs;
	uint32_t	gs;
	uint32_t	ldt;
	uint16_t	trap;
	uint16_t	iomap_base;
} __attribute__ ((packed)) tss_entry_t;

extern void tasking_install();
extern void switch_task(uint8_t reschedule);
extern void switch_from_cross_thread_lock();
extern void switch_next();
extern uint32_t fork();
extern uint32_t clone(uintptr_t new_stack, uintptr_t thread_func, uintptr_t arg);
extern uint32_t getpid();
extern void enter_user_jmp(uintptr_t location, int argc, char ** argv, uintptr_t stack);

uintptr_t initial_esp;

/* Kernel Argument Parser */
extern void parse_args(char * argv);

/* CMOS */
extern void get_time(uint16_t * hours, uint16_t * minutes, uint16_t * seconds);
extern void get_date(uint16_t * month, uint16_t * day);

struct timeval {
	uint32_t tv_sec;
	uint32_t tv_usec;
};

extern int gettimeofday(struct timeval * t, void * z);
extern uint32_t now();


/* CPU Detect by Brynet */
extern int detect_cpu();

/* Video Drivers */
/* Generic (pre-set, 32-bit, linear frame buffer) */
extern void graphics_install_preset(uint16_t, uint16_t);
extern uint16_t lfb_resolution_x;
extern uint16_t lfb_resolution_y;
extern uint16_t lfb_resolution_b;
extern uintptr_t lfb_get_address();

/* BOCHS / QEMU VBE Driver */
extern void graphics_install_bochs(uint16_t, uint16_t);
extern void bochs_set_y_offset(uint16_t y);
extern uint16_t bochs_current_scroll();

/* ANSI Terminal Escape Processor */
void ansi_put(char c);
void ansi_print(char * c);
void ansi_init(void (*writer)(char), int w, int y, void (*setcolor)(unsigned char, unsigned char), void (*setcsr)(int,int), int (*getcsrx)(void), int (*getcsry)(void), void (*setcell)(int,int,char), void (*cls)(void), void (*redraw_csr)(void));
int  ansi_ready;
void (*redraw_cursor)(void);

extern uint8_t number_font[][12];

/* Floating Point Unit */

void set_fpu_cw(const uint16_t);
void enable_fpu();

/* ELF */
int exec( char *, int, char **, char **);
int system( char *, int, char **);

/* Sytem Calls */
void syscalls_install();

/* PCI */
uint16_t pci_read_word(uint32_t bus, uint32_t slot, uint32_t func, uint16_t offset);
void pci_write_word(uint32_t bus, uint32_t slot, uint32_t func, uint16_t offset, uint32_t data);

/* IDE / PATA */
void ide_init(uint16_t bus);
void ide_read_sector(uint16_t bus, uint8_t slave, uint32_t lba, uint8_t * buf);
void ide_write_sector(uint16_t bus, uint8_t slave, uint32_t lba, uint8_t * buf);
void ide_write_sector_retry(uint16_t bus, uint8_t slave, uint32_t lba, uint8_t * buf);

/* vm86 Helpers */
typedef uint32_t  FARPTR;
typedef uintptr_t addr_t;
#define MK_FP(seg, off)        ((FARPTR) (((uint32_t) (seg) << 16) | (uint16_t) (off)))
#define FP_SEG(fp)             (((FARPTR) fp) >> 16)
#define FP_OFF(fp)             (((FARPTR) fp) & 0xffff)
#define FP_TO_LINEAR(seg, off) ((void*) ((((uint16_t) (seg)) << 4) + ((uint16_t) (off))))
#define LINEAR_TO_FP(ptr)      (MK_FP(((addr_t) (ptr) - ((addr_t) (ptr) & 0xf)) / 16, ((addr_t)(ptr) & 0xf)))

typedef struct {
	uint16_t off;
	uint16_t seg;
} rm_ptr_t;

/* wakeup queue */
int wakeup_queue(list_t * queue);
int sleep_on(list_t * queue);

typedef struct {
	uint32_t  signum;
	uintptr_t handler;
	regs_t registers_before;
} signal_t;

void handle_signal(process_t *, signal_t *);

#define USER_STACK_TOP    0x10010000
#define USER_STACK_BOTTOM 0x10000000

void validate(void * ptr);


#endif
