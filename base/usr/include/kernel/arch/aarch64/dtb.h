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

static inline uint32_t swizzle(uint32_t from) {
	uint8_t a = from >> 24;
	uint8_t b = from >> 16;
	uint8_t c = from >> 8;
	uint8_t d = from;
	return (d << 24) | (c << 16) | (b << 8) | (a);
}

static inline uint64_t swizzle64(uint64_t from) {
	uint8_t a = from >> 56;
	uint8_t b = from >> 48;
	uint8_t c = from >> 40;
	uint8_t d = from >> 32;
	uint8_t e = from >> 24;
	uint8_t f = from >> 16;
	uint8_t g = from >> 8;
	uint8_t h = from;
	return ((uint64_t)h << 56) | ((uint64_t)g << 48) | ((uint64_t)f << 40) | ((uint64_t)e << 32) | (d << 24) | (c << 16) | (b << 8) | (a);
}

static inline uint16_t swizzle16(uint16_t from) {
	uint8_t a = from >> 8;
	uint8_t b = from;
	return (b << 8) | (a);
}

uint32_t * dtb_find_node(const char * name);
uint32_t * dtb_find_node_prefix(const char * name);
uint32_t * dtb_node_find_property(uint32_t * node, const char * property);
void dtb_memory_size(size_t * memsize, size_t * physsize);
void dtb_callback_direct_children(uint32_t * node, void (*callback)(uint32_t * child));
void dtb_locate_cmdline(char ** args_out);
