/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2012-2014 Kevin Lange
 * Copyright (C) 2012 Markus Schober
 *
 * Shared Memory
 */
#include <system.h>
#include <process.h>
#include <logging.h>
#include <shm.h>
#include <mem.h>
#include <tree.h>
#include <list.h>


//static volatile uint8_t bsl; // big shm lock
static spin_lock_t bsl; // big shm lock
tree_t * shm_tree = NULL;


void shm_install(void) {
	debug_print(NOTICE, "Installing shared memory layer...");
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
	debug_print(WARNING, "Size supplied to create_chunk was 0");
	if (!size) return NULL;

	shm_chunk_t *chunk = malloc(sizeof(shm_chunk_t));
	if (chunk == NULL) {
		debug_print(ERROR, "Failed to allocate a shm_chunk_t!");
		return NULL;
	}

	chunk->parent = parent;
	chunk->lock = 0;
	chunk->ref_count = 1;

	chunk->num_frames = (size / 0x1000) + ((size % 0x1000) ? 1 : 0);
	chunk->frames = malloc(sizeof(uintptr_t) * chunk->num_frames);
	if (chunk->frames == NULL) {
		debug_print(ERROR, "Failed to allocate uintptr_t[%d]", chunk->num_frames);
		free(chunk);
		return NULL;
	}

	/* Now grab some frames for this guy. */
	for (uint32_t i = 0; i < chunk->num_frames; i++) {
		page_t tmp = {0,0,0,0,0,0,0};
		alloc_frame(&tmp, 0, 0);
		chunk->frames[i] = tmp.frame;
#if 0
		debug_print(WARNING, "Using frame 0x%x for chunk[%d] (name=%s)", tmp.frame * 0x1000, i, parent->name);
#endif
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
				clear_frame(chunk->frames[i] * 0x1000);
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

static uintptr_t proc_sbrk(uint32_t num_pages, process_t * proc) {
	uintptr_t initial = proc->image.shm_heap;
	assert(!(initial & 0xFFF) && "shm_heap not page-aligned!");

	if (initial % 0x1000) {
		initial += 0x1000 - (initial % 0x1000);
		proc->image.shm_heap = initial;
	}
	proc->image.shm_heap += num_pages * 0x1000;
	assert(!(proc->image.shm_heap & 0xFFF) && "math is wrong, dumbass");

	return initial;
}

static void * map_in (shm_chunk_t * chunk, process_t * proc) {
	if (!chunk) {
		return NULL;
	}

	shm_mapping_t * mapping = malloc(sizeof(shm_mapping_t));
	mapping->chunk = chunk;
	mapping->num_vaddrs = chunk->num_frames;
	mapping->vaddrs = malloc(sizeof(uintptr_t) * mapping->num_vaddrs);

	debug_print(INFO, "want %d bytes, running through mappings...", mapping->num_vaddrs * 0x1000);
	uintptr_t last_address = SHM_START;
	foreach(node, proc->shm_mappings) {
		shm_mapping_t * m = node->value;
		if (m->vaddrs[0] > last_address) {
			size_t gap = (uintptr_t)m->vaddrs[0] - last_address;
			debug_print(INFO, "gap found at 0x%x of size %d", last_address, gap);
			if (gap >= mapping->num_vaddrs * 0x1000) {
				debug_print(INFO, "Gap is sufficient, we can insert here.");

				/* Map the gap */
				for (unsigned int i = 0; i < chunk->num_frames; ++i) {
					page_t * page = get_page(last_address + i * 0x1000, 1, proc->thread.page_directory);
					page->frame = chunk->frames[i];
					alloc_frame(page, 0, 1);
					invalidate_tables_at(last_address + i * 0x1000);
					mapping->vaddrs[i] = last_address + i * 0x1000;
				}

				/* Insert us before this node */
				list_insert_before(proc->shm_mappings, node, mapping);

				return (void *)mapping->vaddrs[0];
			}
		}
		last_address = m->vaddrs[0] + m->num_vaddrs * 0x1000;
		debug_print(INFO, "[0x%x:0x%x] %s", m->vaddrs[0], last_address, m->chunk->parent->name);
	}
	if (proc->image.shm_heap > last_address) {
		size_t gap = proc->image.shm_heap - last_address;
		debug_print(INFO, "gap found at 0x%x of size %d", last_address, gap);
		if (gap >= mapping->num_vaddrs * 0x1000) {
			debug_print(INFO, "Gap is sufficient, we can insert here.");

			for (unsigned int i = 0; i < chunk->num_frames; ++i) {
				page_t * page = get_page(last_address + i * 0x1000, 1, proc->thread.page_directory);
				page->frame = chunk->frames[i];
				alloc_frame(page, 0, 1);
				invalidate_tables_at(last_address + i * 0x1000);
				mapping->vaddrs[i] = last_address + i * 0x1000;
			}

			list_insert(proc->shm_mappings, mapping);

			return (void *)mapping->vaddrs[0];
		} else {
			debug_print(INFO, "should be more efficient here - there is space available, but we are not going to use it");
		}
	}


	for (uint32_t i = 0; i < chunk->num_frames; i++) {
		uintptr_t new_vpage = proc_sbrk(1, proc);
		assert(new_vpage % 0x1000 == 0);

		page_t * page = get_page(new_vpage, 1, proc->thread.page_directory);
		assert(page && "Page not allocated by sys_sbrk?");

		page->frame = chunk->frames[i];
		alloc_frame(page, 0, 1);
		invalidate_tables_at(new_vpage);
		mapping->vaddrs[i] = new_vpage;

#if 0
			debug_print(INFO, "mapping vaddr 0x%x --> #%d", new_vpage, page->frame);
#endif
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
	process_t * proc = (process_t *)current_process;

	if (proc->group != 0) {
		proc = process_from_pid(proc->group);
	}

	shm_node_t * node = get_node(path, 1); // (if it exists, just get it)
	assert(node && "shm_node_t not created by get_node");
	shm_chunk_t * chunk = node->chunk;

	if (chunk == NULL) {
		/* There's no chunk for that key -- we need to allocate it! */

		debug_print(NOTICE, "Allocating a new shmem chunk for process %d", proc->id);

		if (size == 0) {
			// The process doesn't want a chunk...?
			spin_unlock(bsl);
			return NULL;
		}

		chunk = create_chunk(node, *size);
		if (chunk == NULL) {
			debug_print(ERROR, "Could not allocate a shm_chunk_t");
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
	invalidate_page_tables();

	return vshm_start;
}

int shm_release (char * path) {
	spin_lock(bsl);
	process_t * proc = (process_t *)current_process;

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
		page_t * page = get_page(mapping->vaddrs[i], 0, proc->thread.page_directory);
		assert(page && "Shared memory mapping was invalid!");

		memset(page, 0, sizeof(page_t));
	}
	invalidate_page_tables();

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


