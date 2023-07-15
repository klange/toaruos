/**
 * @file  kernel/arch/aarch64/dtb.c
 * @brief Methods for parsing device tree binaries
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2022 K. Lange
 */
#include <stdint.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/mmu.h>
#include <kernel/misc.h>
#include <kernel/args.h>
#include <kernel/vfs.h>

#include <kernel/arch/aarch64/dtb.h>

uintptr_t aarch64_dtb_phys = 0;

static uint32_t * parse_node(uint32_t * node, char * strings, int x) {
	while (swizzle(*node) == 4) node++;
	if (swizzle(*node) == 9) return NULL;
	if (swizzle(*node) != 1) {
		printf("Not a node? Got %x\n", swizzle(*node));
		return NULL;
	}

	/* Skip the BEGIN_NODE */
	node++;

	for (int i = 0; i < x; ++i) printf("  ");

	while (1) {
		char * x = (char*)node;
		if (x[0]) { printf("%c",x[0]); } else { node++; break; }
		if (x[1]) { printf("%c",x[1]); } else { node++; break; }
		if (x[2]) { printf("%c",x[2]); } else { node++; break; }
		if (x[3]) { printf("%c",x[3]); } else { node++; break; }
		node++;
	}
	printf("\n");

	while (1) {
		while (swizzle(*node) == 4) node++;
		if (swizzle(*node) == 2) return node+1;
		if (swizzle(*node) == 3) {
			for (int i = 0; i < x; ++i) printf("  ");
			uint32_t len = swizzle(node[1]);
			uint32_t nameoff = swizzle(node[2]);
			printf("  property %s len=%u\n", strings + nameoff, len);
			node += 3;
			node += (len + 3) / 4;
		} else if (swizzle(*node) == 1) {
			node = parse_node(node, strings, x + 1);
		}
	}

}

static uint32_t * find_subnode(uint32_t * node, char * strings, const char * name, uint32_t ** node_out, int (*cmp)(const char* a, const char *b)) {
	while (swizzle(*node) == 4) node++;
	if (swizzle(*node) == 9) return NULL;
	if (swizzle(*node) != 1) return NULL;
	node++;

	if (cmp((char*)node,name)) {
		*node_out = node;
		return NULL;
	}

	while ((*node & 0xFF000000) && (*node & 0xFF0000) && (*node & 0xFF00) && (*node & 0xFF)) node++;
	node++;

	while (1) {
		while (swizzle(*node) == 4) node++;
		if (swizzle(*node) == 2) return node+1;
		if (swizzle(*node) == 3) {
			uint32_t len = swizzle(node[1]);
			node += 3;
			node += (len + 3) / 4;
		} else if (swizzle(*node) == 1) {
			node = find_subnode(node, strings, name, node_out, cmp);
			if (!node) return NULL;
		}
	}
}

static uint32_t * skip_node(uint32_t * node, void (*callback)(uint32_t * child)) {
	while (swizzle(*node) == 4) node++;
	if (swizzle(*node) == 9) return NULL;
	if (swizzle(*node) != 1) return NULL;
	node++;
	while ((*node & 0xFF000000) && (*node & 0xFF0000) && (*node & 0xFF00) && (*node & 0xFF)) node++;
	node++;
	while (1) {
		while (swizzle(*node) == 4) node++;
		if (swizzle(*node) == 2) return node+1;
		if (swizzle(*node) == 3) {
			uint32_t len = swizzle(node[1]);
			node += 3;
			node += (len + 3) / 4;
		} else if (swizzle(*node) == 1) {
			if (callback) callback(node+1);
			node = skip_node(node, NULL);
			if (!node) return NULL;
		}
	}
}

void dtb_callback_direct_children(uint32_t * node, void (*callback)(uint32_t * child)) {
	skip_node(node-1, callback);
}


static uint32_t * find_node_int(const char * name, int (*cmp)(const char*,const char*)) {
	uintptr_t addr = (uintptr_t)mmu_map_from_physical(aarch64_dtb_phys);
	struct fdt_header * fdt = (struct fdt_header*)addr;
	char * dtb_strings = (char *)(addr + swizzle(fdt->off_dt_strings));
	uint32_t * dtb_struct = (uint32_t *)(addr + swizzle(fdt->off_dt_struct));

	uint32_t * out = NULL;
	find_subnode(dtb_struct, dtb_strings, name, &out, cmp);
	return out;
}

static int base_cmp(const char *a, const char *b) {
	return !strcmp(a,b);
}

uint32_t * dtb_find_node(const char * name) {
	return find_node_int(name,base_cmp);
}

static int prefix_cmp(const char *a, const char *b) {
	return !memcmp(a,b,strlen(b));
}

uint32_t * dtb_find_node_prefix(const char * name) {
	return find_node_int(name,prefix_cmp);
}

static uint32_t * node_find_property_int(uint32_t * node, char * strings, const char * property, uint32_t ** out) {
	while ((*node & 0xFF000000) && (*node & 0xFF0000) && (*node & 0xFF00) && (*node & 0xFF)) node++;
	node++;

	while (1) {
		while (swizzle(*node) == 4) node++;
		if (swizzle(*node) == 2) return node+1;
		if (swizzle(*node) == 3) {
			uint32_t len = swizzle(node[1]);
			uint32_t nameoff = swizzle(node[2]);
			if (!strcmp(strings + nameoff, property)) {
				*out = &node[1];
				return NULL;
			}
			node += 3;
			node += (len + 3) / 4;
		} else if (swizzle(*node) == 1) {
			node = node_find_property_int(node+1, strings, property, out);
			if (!node) return NULL;
		}
	}
}

uint32_t * dtb_node_find_property(uint32_t * node, const char * property) {
	uintptr_t addr = (uintptr_t)mmu_map_from_physical(aarch64_dtb_phys);
	struct fdt_header * fdt = (struct fdt_header*)addr;
	char * dtb_strings = (char *)(addr + swizzle(fdt->off_dt_strings));
	uint32_t * out = NULL;
	node_find_property_int(node, dtb_strings, property, &out);
	return out;
}

/**
 * Figure out 1) how much actual memory we have and 2) what the last
 * address of physical memory is.
 */
void dtb_memory_size(size_t * memaddr, size_t * physsize) {
	uint32_t * memory = dtb_find_node_prefix("memory");
	if (!memory) {
		printf("dtb: Could not find memory node.\n");
		arch_fatal();
	}

	uint32_t * regs = dtb_node_find_property(memory, "reg");
	if (!regs) {
		printf("dtb: memory node has no regs\n");
		arch_fatal();
	}

	/* Eventually, same with fw-cfg, we'll need to actually figure out
	 * the size of the 'reg' cells, but at least right now it's been
	 * 2 address, 2 size. */
	uint64_t mem_addr = (uint64_t)swizzle(regs[3]) | ((uint64_t)swizzle(regs[2]) << 32UL);
	uint64_t mem_size = (uint64_t)swizzle(regs[5]) | ((uint64_t)swizzle(regs[4]) << 32UL);

	*memaddr = mem_addr;
	*physsize = mem_size;
}

void dtb_locate_cmdline(char ** args_out) {
	uint32_t * chosen = dtb_find_node("chosen");
	if (chosen) {
		uint32_t * prop = dtb_node_find_property(chosen, "bootargs");
		if (prop) {
			*args_out = (char*)&prop[2];
			args_parse((char*)&prop[2]);
		}
	}
}

static ssize_t read_dtb(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
	if ((size_t)offset > node->length) {
		return 0;
	}
	if ((size_t)offset + size > node->length) {
		size_t i = node->length - offset;
		size = i;
	}
	memcpy(buffer, (void *)((uintptr_t)mmu_map_from_physical(aarch64_dtb_phys) + (uintptr_t)offset), size);
	return size;
}

void dtb_device(void) {
	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	memset(fnode, 0x00, sizeof(fs_node_t));
	snprintf(fnode->name, 10, "dtb");
	fnode->inode = 0;
	fnode->device = fnode;
	fnode->uid = 0;
	fnode->gid = 0;
	fnode->mask    = 0770;
	fnode->length  = 1048576;
	fnode->flags   = FS_BLOCKDEVICE;
	fnode->read    = read_dtb;
	vfs_mount("/dev/dtb", fnode);
}

