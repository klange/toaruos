#pragma once
#include <stdint.h>

struct fdt_header {
	uint32_t magic;
	uint32_t totalsize;
	uint32_t off_dt_struct;
	uint32_t off_dt_strings;
	uint32_t off_mem_rsvmap;
	uint32_t version;
	uint32_t last_comp_version;
	uint32_t boot_cpuid_phys;
	uint32_t size_dt_strings;
	uint32_t size_dt_struct;
};

uint32_t * find_node(const char * name);
uint32_t * find_node_prefix(const char * name);
uint32_t * node_find_property(uint32_t * node, const char * property);
void dtb_memory_size(size_t * memsize, size_t * physsize);
