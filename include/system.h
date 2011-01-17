#ifndef __SYSTEM_H
#define __SYSTEM_H

/* Kernel Main */
extern unsigned char *memcpy(unsigned char *dest, const unsigned char *src, int count);
extern unsigned char *memset(unsigned char *dest, unsigned char val, int count);
extern unsigned short *memsetw(unsigned short *dest, unsigned short val, int count);
extern int strlen(const char *str);
extern unsigned char inportb (unsigned short _port);
extern void outportb (unsigned short _port, unsigned char _data);

/* VGA driver */
extern void cls();
extern void putch(unsigned char c);
extern void puts(char *str);
extern void settextcolor(unsigned char forecolor, unsigned char backcolor);
extern void init_video();

/* GDT */
extern void gdt_install();
extern void gdt_set_gate(int num, unsigned long base, unsigned long limit, unsigned char access, unsigned char gran);

/* IDT */
extern void idt_install();
extern void idt_set_gate(unsigned char num, unsigned long base, unsigned short sel, unsigned char flags);

/* Registers */
struct regs {
    unsigned int gs, fs, es, ds;
    unsigned int edi, esi, ebp, esp, ebx, edx, ecx, eax;
    unsigned int int_no, err_code;
    unsigned int eip, cs, eflags, useresp, ss;
};

/* ISRS */
extern void isrs_install();

/* Interrupt Handlers */
extern void irq_install();
extern void irq_install_handler(int irq, void *handler);
extern void irq_uninstall_handler(int irq);

/* Timer */
extern void timer_install();
extern int timer_ticks;
extern void timer_wait(int ticks);

/* Keyboard */
extern void keyboard_install();
extern void keyboard_wait();

/* kprintf */
extern void kprintf(const char *fmt, ...);

#endif
