#pragma once

static inline unsigned short inports(unsigned short _port) {
	unsigned short rv;
	asm volatile ("inw %1, %0" : "=a" (rv) : "dN" (_port));
	return rv;
}

static inline void outports(unsigned short _port, unsigned short _data) {
	asm volatile ("outw %1, %0" : : "dN" (_port), "a" (_data));
}

static inline unsigned int inportl(unsigned short _port) {
	unsigned int rv;
	asm volatile ("inl %%dx, %%eax" : "=a" (rv) : "dN" (_port));
	return rv;
}

static inline void outportl(unsigned short _port, unsigned int _data) {
	asm volatile ("outl %%eax, %%dx" : : "dN" (_port), "a" (_data));
}

static inline unsigned char inportb(unsigned short _port) {
	unsigned char rv;
	asm volatile ("inb %1, %0" : "=a" (rv) : "dN" (_port));
	return rv;
}

static inline void outportb(unsigned short _port, unsigned char _data) {
	asm volatile ("outb %1, %0" : : "dN" (_port), "a" (_data));
}

static inline void inportsm(unsigned short port, unsigned char * data, unsigned long size) {
	asm volatile ("rep insw" : "+D" (data), "+c" (size) : "d" (port) : "memory");
}

void * memcpy(void * restrict dest, const void * restrict src, long n);
void * memset(void * dest, int c, long n);
int strcmp(const char * l, const char * r);
char * strchr(const char * s, int c);
char * strcat(char *dest, const char *src);
void copy_sectors(unsigned long lba, unsigned char * buf, int sectors);
void copy_sector(unsigned long lba, unsigned char * buf);
unsigned long strlen(const char *s);

#define DATA_LOAD_BASE 0x4000000
