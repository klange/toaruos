/**
 * @file  kernel/sys/shm.c
 * @brief Shared memory subsystem
 *
 * Provides shared memory mappings for userspace processes and
 * manages their allocation/deallocation for process cleanup.
 * Used primarily to implement text buffers for the compositor.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2012-2021 K. Lange
 * Copyright (C) 2012 Markus Schober
 */
#include <stdint.h>
#include <stdlib.h>
#include <kernel/types.h>
#include <kernel/printf.h>
#include <kernel/process.h>
#include <kernel/mmu.h>
#include <kernel/shm.h>
#include <kernel/spinlock.h>
#include <kernel/string.h>

#include <kernel/tree.h>
#include <kernel/list.h>


//static volatile uint8_t bsl; // big shm lock
static spin_lock_t bsl; // big shm lock
tree_t * shm_tree = NULL;


void shm_install(void) {
	shm_tree = tree_create();
	tree_set_root(shm_tree, NULL);
}


/* Accessors */


static shm_node_t * _get_node(char * shm_path, int create, tree_node_t * from) {

	char *pch, *save;
	pch = strtok_r(shm_path, SHM_PATH_SEPARATOR, &save);

	tree_node_t * tnode = from;
	foreach(node, tnode->children) {
		tree_node_t * _node = (tree_node_t *)node->value;
		shm_node_t *  snode = (shm_node_t *)_node->value;

		if (!strcmp(snode->name, pch)) {
			if (*save == '\0') {
				return snode;
			}
			return _get_node(save, create, _node);
		}
	}

	/* The next node in sequence was not found */
	if (create) {
		shm_node_t * nsnode = malloc(sizeof(shm_node_t));
		memcpy(nsnode->name, pch, strlen(pch) + 1);
		nsnode->chunk = NULL;

		tree_node_t * nnode = tree_node_insert_child(shm_tree, from, nsnode);

		if (*save == '\0') {
			return nsnode;
		}

		return _get_node(save, create, nnode);
	} else {
		return NULL;
	}
}

static shm_node_t * get_node (char * shm_path, int create) {
	char * _path = malloc(strlen(shm_path)+1);
	memcpy(_path, shm_path, strlen(shm_path)+1);

	shm_node_t * node = _get_node(_path, create, shm_tree->root);

	free(_path);
	return node;
}


/* Create and Release */


static shm_chunk_t * create_chunk (shm_node_t * parent, size_t size) {
	if (!size) return NULL;

	shm_chunk_t *chunk = malloc(sizeof(shm_chunk_t));
	if (chunk == NULL) {
		return NULL;
	}

	chunk->parent = parent;
	chunk->lock = 0;
	chunk->ref_count = 1;

	chunk->num_frames = (size / 0x1000) + ((size % 0x1000) ? 1 : 0);
	chunk->frames = malloc(sizeof(uintptr_t) * chunk->num_frames);
	if (chunk->frames == NULL) {
		free(chunk);
		return NULL;
	}

	/* Now grab some frames for this guy. */
	for (uint32_t i = 0; i < chunk->num_frames; i++) {
		/* Allocate frame */
		uintptr_t index = mmu_allocate_a_frame();
		chunk->frames[i] = index;
	}

	return chunk;
}

static int release_chunk (shm_chunk_t * chunk) {
	if (chunk) {
		chunk->ref_count--;

		/* Does the chunk need to be freed? */
		if (chunk->ref_count < 1) {
#if 0
			debug_print(INFO, "Freeing chunk with name %s", chunk->parent->name);
#endif

			/* First, free the frames used by this chunk */
			for (uint32_t i = 0; i < chunk->num_frames; i++) {
				mmu_frame_release(chunk->frames[i] << 12);
			}

			/* Then, get rid of the damn thing */
			chunk->parent->chunk = NULL;
			free(chunk->frames);
			free(chunk);
		}

		return 0;
	}

	return -1;
}


/* Mapping and Unmapping */

static uintptr_t proc_sbrk(uint32_t num_pages, volatile process_t * volatile proc) {
	uintptr_t initial = proc->image.shm_heap;

	if (initial & 0xFFF) {
		initial += 0x1000 - (initial & 0xFFF);
		proc->image.shm_heap = initial;
	}
	proc->image.shm_heap += num_pages << 12;

	return initial;
}

static void * map_in (shm_chunk_t * chunk, volatile process_t * volatile proc) {
	if (!chunk) {
		return NULL;
	}

	shm_mapping_t * mapping = malloc(sizeof(shm_mapping_t));
	mapping->chunk = chunk;
	mapping->num_vaddrs = chunk->num_frames;
	mapping->vaddrs = malloc(sizeof(uintptr_t) * mapping->num_vaddrs);

	uintptr_t last_address = USER_SHM_LOW;
	foreach(node, proc->shm_mappings) {
		shm_mapping_t * m = node->value;
		if (m->vaddrs[0] > last_address) {
			size_t gap = (uintptr_t)m->vaddrs[0] - last_address;
			if (gap >= mapping->num_vaddrs * 0x1000) {
				/* Map the gap */
				for (unsigned int i = 0; i < chunk->num_frames; ++i) {
					union PML * page = mmu_get_page(last_address + (i << 12), MMU_GET_MAKE);
					page->bits.page = chunk->frames[i];
					mmu_frame_allocate(page, MMU_FLAG_WRITABLE);
					mapping->vaddrs[i] = last_address + (i << 12);
				}

				/* Insert us before this node */
				list_insert_before(proc->shm_mappings, node, mapping);

				return (void *)mapping->vaddrs[0];
			}
		}
		last_address = m->vaddrs[0] + m->num_vaddrs * 0x1000;
	}

	if (proc->image.shm_heap > last_address) {
		size_t gap = proc->image.shm_heap - last_address;
		if (gap >= mapping->num_vaddrs * 0x1000) {

			for (unsigned int i = 0; i < chunk->num_frames; ++i) {
				union PML * page = mmu_get_page(last_address + (i << 12), MMU_GET_MAKE);
				page->bits.page = chunk->frames[i];
				mmu_frame_allocate(page, MMU_FLAG_WRITABLE);
				mapping->vaddrs[i] = last_address + (i << 12);
			}

			list_insert(proc->shm_mappings, mapping);
			return (void *)mapping->vaddrs[0];
		}
	}


	for (uint32_t i = 0; i < chunk->num_frames; i++) {
		uintptr_t new_vpage = proc_sbrk(1, proc);

		union PML * page = mmu_get_page(new_vpage, MMU_GET_MAKE);
		page->bits.page = chunk->frames[i];
		mmu_frame_allocate(page, MMU_FLAG_WRITABLE);
		mapping->vaddrs[i] = new_vpage;
	}

	list_insert(proc->shm_mappings, mapping);

	return (void *)mapping->vaddrs[0];
}

static size_t chunk_size (shm_chunk_t * chunk) {
	return (size_t)(chunk->num_frames * 0x1000);
}


/* Kernel-Facing Functions and Syscalls */


void * shm_obtain (char * path, size_t * size) {
	spin_lock(bsl);
	volatile process_t * volatile proc = this_core->current_process;

	if (proc->group != 0) {
		proc = process_from_pid(proc->group);
	}

	shm_node_t * node = get_node(path, 1); // (if it exists, just get it)
	shm_chunk_t * chunk = node->chunk;

	if (chunk == NULL) {
		/* There's no chunk for that key -- we need to allocate it! */

		if (!size) {
			// The process doesn't want a chunk...?
			spin_unlock(bsl);
			return NULL;
		}

		chunk = create_chunk(node, *size);
		if (chunk == NULL) {
			spin_unlock(bsl);
			return NULL;
		}

		node->chunk = chunk;
	} else {
		/* New accessor! */
		chunk->ref_count++;
	}

	void * vshm_start = map_in(chunk, proc);
	*size = chunk_size(chunk);

	spin_unlock(bsl);

	return vshm_start;
}

int shm_release (char * path) {
	spin_lock(bsl);
	process_t * proc = (process_t *)this_core->current_process;

	if (proc->group != 0) {
		proc = process_from_pid(proc->group);
	}

	/* First, find the right chunk */
	shm_node_t * _node = get_node(path, 0);
	if (!_node) {
		spin_unlock(bsl);
		return 1;
	}
	shm_chunk_t * chunk = _node->chunk;

	/* Next, find the proc's mapping for that chunk */
	node_t * node = NULL;
	foreach (n, proc->shm_mappings) {
		shm_mapping_t * m = (shm_mapping_t *)n->value;
		if (m->chunk == chunk) {
			node = n;
			break;
		}
	}
	if (node == NULL) {
		spin_unlock(bsl);
		return 1;
	}

	shm_mapping_t * mapping = (shm_mapping_t *)node->value;

	/* Clear the mappings from the process's address space */
	for (uint32_t i = 0; i < mapping->num_vaddrs; i++) {
		union PML * page = mmu_get_page(mapping->vaddrs[i], 0);
		page->bits.present = 0;
		mmu_invalidate(mapping->vaddrs[i]);
	}

	/* Clean up */
	release_chunk(chunk);
	list_delete(proc->shm_mappings, node);
	free(node);
	free(mapping);

	spin_unlock(bsl);
	return 0;
}

/* This function should only be called if the process's address space
 * is about to be destroyed -- chunks will not be unmounted therefrom ! */
void shm_release_all (process_t * proc) {
	spin_lock(bsl);

	node_t * node;
	while ((node = list_pop(proc->shm_mappings)) != NULL) {
		shm_mapping_t * mapping = node->value;
		release_chunk(mapping->chunk);
		free(mapping);
		free(node);
	}

	/* Empty, but don't free, the mappings list */
	list_free(proc->shm_mappings);
	proc->shm_mappings->head = proc->shm_mappings->tail = NULL;
	proc->shm_mappings->length = 0;

	spin_unlock(bsl);
}


