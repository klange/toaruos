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
#include <kernel/process.h>
#include <kernel/vfs.h>
#include <kernel/string.h>

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

static ssize_t read_port(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
	switch (size) {
		case 1:
			buffer[0] = inportb(offset);
			break;
		case 2:
			((uint16_t *)buffer)[0] = inports(offset);
			break;
		case 4:
			((uint32_t *)buffer)[0] = inportl(offset);
			break;
		default:
			for (unsigned int i = 0; i < size; ++i) {
				buffer[i] = inportb(offset + i);
			}
			break;
	}

	return size;
}

static ssize_t write_port(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
	switch (size) {
		case 1:
			outportb(offset, buffer[0]);
			break;
		case 2:
			outports(offset, ((uint16_t*)buffer)[0]);
			break;
		case 4:
			outportl(offset, ((uint32_t*)buffer)[0]);
			break;
		default:
			for (unsigned int i = 0; i < size; ++i) {
				outportb(offset +i, buffer[i]);
			}
			break;
	}

	return size;
}

static fs_node_t * port_device_create(void) {
	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	memset(fnode, 0x00, sizeof(fs_node_t));
	fnode->inode = 0;
	strcpy(fnode->name, "port");
	fnode->uid = 0;
	fnode->gid = 0;
	fnode->mask = 0660;
	fnode->flags   = FS_BLOCKDEVICE;
	fnode->read    = read_port;
	fnode->write   = write_port;
	fnode->open    = NULL;
	fnode->close   = NULL;
	fnode->readdir = NULL;
	fnode->finddir = NULL;
	fnode->ioctl   = NULL;
	return fnode;
}

void portio_initialize(void) {
	vfs_mount("/dev/port", port_device_create(), "port", "");
}
