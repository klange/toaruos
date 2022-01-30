#pragma once
#include <stdint.h>

union PML {
	struct {
		uint64_t present : 1;
		uint64_t table_page : 1;
		uint64_t attrindx:3;
		uint64_t ns:1;
		uint64_t ap:2;
		uint64_t sh:2;
		uint64_t af:1;
		uint64_t ng:1;
		uint64_t page:36;
		uint64_t reserved:4;
		uint64_t contiguous:1;
		uint64_t pxn:1;
		uint64_t uxn:1;
		uint64_t avail:4;
		uint64_t ignored:5;
	} bits;

	struct {
		uint64_t valid : 1;
		uint64_t table : 1;
		uint64_t next:46;
		uint64_t reserved:4;
		uint64_t ignored:7;
		uint64_t pxntable:1;
		uint64_t xntable:1;
		uint64_t aptable:2;
		uint64_t nstable:1;
	} table_bits;

	uint64_t raw;
};

#define mmu_page_is_user_readable(p) (p->bits.ap & 1)
#define mmu_page_is_user_writable(p) ((p->bits.ap & 1) && !(p->bits.ap & 2))
