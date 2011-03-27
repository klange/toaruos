#ifndef __SYSTEM_H
#define __SYSTEM_H
#define _KERNEL_
#include <types.h>


/* Unimportant Kernel Strings */
#define KERNEL_UNAME "ToAruOS"
#define KERNEL_VERSION_STRING "0.0.1"

extern void *sbrk(uintptr_t increment);

/* Kernel Main */
extern void *memcpy(void *restrict dest, const void *restrict src, size_t count);
extern void *memset(void *dest, int val, size_t count);
extern unsigned short *memsetw(unsigned short *dest, unsigned short val, int count);
extern int strlen(const char *str);
extern int atoi(const char *str);
extern unsigned char inportb(unsigned short _port);
extern void outportb(unsigned short _port, unsigned char _data);
extern unsigned short inports(unsigned short _port);
extern void outports(unsigned short _port, unsigned short _data);
extern int strcmp(const char *a, const char *b);
extern char * strtok_r(char * str, const char * delim, char ** saveptr);
extern size_t lfind(const char * str, const char accept);
extern size_t strspn(const char * str, const char * accept);
extern char * strpbrk(const char * str, const char * accept);
extern uint32_t krand();

/* Panic */
#define HALT_AND_CATCH_FIRE(mesg) halt_and_catch_fire(mesg, __FILE__, __LINE__)
#define ASSERT(statement) ((statement) ? (void)0 : assert_failed(__FILE__, __LINE__, #statement))
#define assert(statement) ((statement) ? (void)0 : assert_failed(__FILE__, __LINE__, #statement))
void halt_and_catch_fire(char *error_message, const char *file, int line);
void assert_failed(const char *file, uint32_t line, const char *desc);

/* VGA driver */
extern void cls();
extern void puts(char *str);
extern void settextcolor(unsigned char forecolor, unsigned char backcolor);
extern void resettextcolor();
extern void brighttextcolor();
extern void init_video();
extern void placech(unsigned char c, int x, int y, int attr);
extern void writechf(unsigned char c);
extern void writech(unsigned char c);
extern void place_csr(uint32_t x, uint32_t y);
extern void store_csr();
extern void restore_csr();
extern void set_serial(int);
extern void set_csr(int);

/* GDT */
extern void gdt_install();
extern void gdt_set_gate(int num, unsigned long base, unsigned long limit, unsigned char access,
			 unsigned char gran);

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

typedef void (*irq_handler_t) (struct regs *);

/* ISRS */
extern void isrs_install();
extern void isrs_install_handler(int isrs, irq_handler_t);
extern void isrs_uninstall_handler(int isrs);

/* Interrupt Handlers */
extern void irq_install();
extern void irq_install_handler(int irq, irq_handler_t);
extern void irq_uninstall_handler(int irq);

/* Timer */
extern void timer_install();
extern long timer_ticks;
extern void timer_wait(int ticks);

/* Keyboard */
typedef void (*keyboard_buffer_t)(char ch);
keyboard_buffer_t keyboard_buffer_handler;
extern void keyboard_install();
extern void keyboard_wait();
extern void putch(unsigned char c);

/* kprintf */
extern void kprintf(const char *fmt, ...);
extern int kgets(char *buf, int size);

/* Memory Management */
extern uintptr_t placement_pointer;
extern void kmalloc_startat(uintptr_t address);
extern uintptr_t kmalloc_real(size_t size, int align, uintptr_t * phys);
extern uintptr_t kmalloc(size_t size);
extern uintptr_t kvmalloc(size_t size);
extern uintptr_t kmalloc_p(size_t size, uintptr_t * phys);
extern uintptr_t kvmalloc_p(size_t size, uintptr_t * phys);

typedef struct page {
	uint32_t present:1;
	uint32_t rw:1;
	uint32_t user:1;
	uint32_t accessed:1;
	uint32_t dirty:1;
	uint32_t unused:7;
	uint32_t frame:20;
} page_t;

typedef struct page_table {
	page_t pages[1024];
} page_table_t;

typedef struct page_directory {
	page_table_t *tables[1024];	/* 1024 pointers to page tables... */
	uintptr_t physical_tables[1024];	/* Physical addresses of the tables */
	uintptr_t physical_address;	/* The physical address of physical_tables */
} page_directory_t;

page_directory_t *kernel_directory;
page_directory_t *current_directory;

extern void paging_install(uint32_t memsize);
extern void switch_page_directory(page_directory_t * new);
extern page_t *get_page(uintptr_t address, int make, page_directory_t * dir);
extern void page_fault(struct regs *r);

void heap_install();

void alloc_frame(page_t *page, int is_kernel, int is_writeable);

/* klmalloc */
void * __attribute__ ((malloc)) malloc(size_t size);
void * __attribute__ ((malloc)) realloc(void *ptr, size_t size);
void * __attribute__ ((malloc)) calloc(size_t nmemb, size_t size);
void * __attribute__ ((malloc)) valloc(size_t size);
void free(void *ptr);

/* shell */
extern void start_shell();

/* Serial */
extern void serial_install();
extern char serial_recv();
extern void serial_send(char out);

/* Tasks */
extern uintptr_t read_eip();
extern void copy_page_physical(uint32_t, uint32_t);
extern page_directory_t * clone_directory(page_directory_t * src);
extern page_table_t * clone_table(page_table_t * src, uintptr_t * physAddr);
extern void move_stack(void *new_stack_start, size_t size);

typedef struct task {
	uint32_t  id;
	uintptr_t esp;
	uintptr_t ebp;
	uintptr_t eip;
	page_directory_t * page_directory;
	struct task *next;
	uintptr_t kernel_stack;
	uintptr_t stack;
} task_t;

extern void tasking_install();
extern void switch_task();
extern uint32_t fork();
extern uint32_t getpid();

uintptr_t initial_esp;

/* Kernel Argument Parser */
extern void parse_args(char * argv);

/* CMOS */
extern void get_time(uint16_t * hours, uint16_t * minutes, uint16_t * seconds);

/* CPU Detect by Brynet */
extern int detect_cpu();

/* Video Drivers */
/* BOCHS / QEMU VBE Driver */
extern void graphics_install_bochs();
extern void bochs_set_bank(uint16_t bank);
extern void bochs_set_coord(uint16_t x, uint16_t y, uint32_t color);
extern void bochs_draw_logo(char *);
extern void bochs_scroll();

#endif
