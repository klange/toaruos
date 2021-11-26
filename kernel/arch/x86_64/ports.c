/**
 * @file  kernel/arch/x86_64/ports.c
 * @brief Port I/O methods for x86-64.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
 */
#include <kernel/types.h>

unsigned short inports(unsigned short _port) {
	unsigned short rv;
	asm volatile ("inw %1, %0" : "=a" (rv) : "dN" (_port));
	return rv;
}

void outports(unsigned short _port, unsigned short _data) {
	asm volatile ("outw %1, %0" : : "dN" (_port), "a" (_data));
}

unsigned int inportl(unsigned short _port) {
	unsigned int rv;
	asm volatile ("inl %%dx, %%eax" : "=a" (rv) : "dN" (_port));
	return rv;
}

void outportl(unsigned short _port, unsigned int _data) {
	asm volatile ("outl %%eax, %%dx" : : "dN" (_port), "a" (_data));
}

unsigned char inportb(unsigned short _port) {
	unsigned char rv;
	asm volatile ("inb %1, %0" : "=a" (rv) : "dN" (_port));
	return rv;
}

void outportb(unsigned short _port, unsigned char _data) {
	asm volatile ("outb %1, %0" : : "dN" (_port), "a" (_data));
}

void outportsm(unsigned short port, unsigned char * data, unsigned long size) {
	asm volatile ("rep outsw" : "+S" (data), "+c" (size) : "d" (port));
}

void inportsm(unsigned short port, unsigned char * data, unsigned long size) {
	asm volatile ("rep insw" : "+D" (data), "+c" (size) : "d" (port) : "memory");
}
