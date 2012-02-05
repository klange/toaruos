#ifndef TASK_H
#define TASK_H

#include <types.h>


typedef struct page {
	uint32_t present:1;
	uint32_t rw:1;
	uint32_t user:1;
	uint32_t accessed:1;
	uint32_t dirty:1;
	uint32_t unused:7;
	uint32_t frame:20;
} __attribute__((packed)) page_t;

typedef struct page_table {
	page_t pages[1024];
} page_table_t;

typedef struct page_directory {
	page_table_t *tables[1024];	/* 1024 pointers to page tables... */
	uintptr_t physical_tables[1024];	/* Physical addresses of the tables */
	uintptr_t physical_address;	/* The physical address of physical_tables */
} page_directory_t;


#endif
