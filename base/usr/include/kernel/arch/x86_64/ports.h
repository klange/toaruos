#pragma once

#include <kernel/types.h>
extern unsigned short inports(unsigned short _port);
extern void outports(unsigned short _port, unsigned short _data);
extern unsigned int inportl(unsigned short _port);
extern void outportl(unsigned short _port, unsigned int _data);
extern unsigned char inportb(unsigned short _port);
extern void outportb(unsigned short _port, unsigned char _data);
extern void outportsm(unsigned short port, unsigned char * data, unsigned long size);
extern void inportsm(unsigned short port, unsigned char * data, unsigned long size);

