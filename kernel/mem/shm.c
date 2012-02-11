/* vim: tabstop=4 shiftwidth=4 noexpandtab
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


static volatile uint8_t bsl; // big shm lock
tree_t * shm_tree = NULL;


void shm_install() {
	LOG(INFO, "Installing SHM");
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
	assert((size > 0) && "Size supplied to create_chunk() was zero!");

	shm_chunk_t *chunk = malloc(sizeof(shm_chunk_t));
	if (chunk == NULL) {
		LOG(ERROR, "[shm] Could not allocate a shm_chunk_t!\n");
		return NULL;
	}

	chunk->parent = parent;
	chunk->lock = 0;
	chunk->ref_count = 1;

	chunk->num_frames = (size / 0x1000) + ((size % 0x1000) ? 1 : 0);
	chunk->frames = malloc(sizeof(uintptr_t) * chunk->num_frames);
	if (chunk->frames == NULL) {
		LOG(ERROR, "[shm] Could not allocate a uintptr_t[%d]!\n", chunk->num_frames);
		free(chunk);
		return NULL;
	}

	/* Now grab some frames for this guy. */
	for (uint32_t i = 0; i < chunk->num_frames; i++) {
		uint32_t index = first_frame();
		set_frame(index * 0x1000);
		chunk->frames[i] = index;
#if 0
		kprintf("Using frame #%d for chunk[%d] (name=%s)\n", index, i, parent->name);
#endif
	}

	return chunk;
}

static int release_chunk (shm_chunk_t * chunk) {
	if (chunk) {
		chunk->ref_count--;

		/* Does the chunk need to be freed? */
		if (chunk->ref_count < 1) {
			kprintf("[shm] Freeing chunk with name %s\n", chunk->parent->name);

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

	for (uint32_t i = 0; i < chunk->num_frames; i++) {
		uintptr_t new_vpage = proc_sbrk(1, proc);
		assert(new_vpage % 0x1000 == 0);

		page_t * page = get_page(new_vpage, 1, proc->thread.page_directory);
		assert(page && "Page not allocated by sys_sbrk?");

		alloc_frame(page, 0, 1);
		page->frame = chunk->frames[i];
		mapping->vaddrs[i] = new_vpage;

#if 0
			kprintf("[kernel] [shm] mapping vaddr 0x%x --> #%d\n", new_vpage, page->frame);
#endif
	}

	list_insert(proc->shm_mappings, mapping);

	return (void *)mapping->vaddrs[0];
}


/* Kernel-Facing Functions and Syscalls */


void * shm_obtain (char * path, size_t size) {
	validate(path);
	spin_lock(&bsl);
	process_t * proc = (process_t *)current_process;

	shm_node_t * node = get_node(path, 1); // (if it exists, just get it)
	assert(node && "shm_node_t not created by get_node");
	shm_chunk_t * chunk = node->chunk;

	if (chunk == NULL) {
		/* There's no chunk for that key -- we need to allocate it! */
		if (size == 0) {
			// The process doesn't want a chunk...?
			return NULL;
		}

		chunk = create_chunk(node, size);
		if (chunk == NULL) {
			LOG(ERROR, "[shm] Could not allocate a shm_chunk_t!\n");
			return NULL;
		}

		node->chunk = chunk;
	} else {
		/* New accessor! */
		chunk->ref_count++;
	}
	void * vshm_start = map_in(chunk, proc);


	spin_unlock(&bsl);

	return vshm_start;
}

int shm_release (char * path) {
	spin_lock(&bsl);
	process_t * proc = (process_t *)current_process;

	/* First, find the right chunk */
	shm_node_t * _node = get_node(path, 0);
	if (!_node) {
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
		return 1;
	}

	shm_mapping_t * mapping = (shm_mapping_t *)node->value;

	/* Clear the mappings from the process's address space */
	for (uint32_t i = 0; i < mapping->num_vaddrs; i++) {
		page_t * page = get_page(mapping->vaddrs[i], 0, proc->thread.page_directory);
		assert(page && "Shared memory mapping was invalid!");

		memset(page, 0, sizeof(page_t));
	}

	/* Clean up */
	release_chunk(chunk);
	list_delete(proc->shm_mappings, node);
	free(node);
	free(mapping);

	spin_unlock(&bsl);
	return 0;
}

/* This function should only be called if the process's address space
 * is about to be destroyed -- chunks will not be unmounted therefrom ! */
void shm_release_all (process_t * proc) {
	spin_lock(&bsl);

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

	spin_unlock(&bsl);
}


/* XXX: oh god don't use this */

#if 0
void shm_debug_frame (uintptr_t vaddr) {
	uintptr_t vframe = vaddr / 0x1000;
	uintptr_t pframe, paddr;

	kprintf("[kernel] Inspecting user page 0x%x\n", vframe * 0x1000);

	uintptr_t table_index = vframe / 1024;

	if (!current_directory->tables[table_index]) {
		kprintf("[kernel] Page does not exist!\n");
		return;
	} else {

		// Where is the vaddr pointing to?
		page_t * page = &current_directory->tables[table_index]->pages[vframe % 1024];
		pframe = page->frame;
		paddr = pframe * 0x1000;

		kprintf("[kernel] Refers to physical frame #%d (present=%d rw=%d user=%d accessed=%d dirty=%d)\n", page->frame, page->present, page->rw, page->user, page->accessed, page->dirty);

#if 0
		// Map the page into kernel memory. Oh god.
		assert((heap_end % 0x1000 == 0) && "Kernel heap not page-aligned!");

		uintptr_t address = heap_end;
		heap_end += 0x1000;
		page_t * kpage = get_page(address, 1, kernel_directory);
		kpage->frame = pframe;
		alloc_frame(kpage, 0, 1);
#endif

		// Read it out...
#if 0
		kprintf("[kernel] Data in frame: ");
		for (int i = 0; i < 0x1000; i++) {
			kprintf("%c", ((char *)vaddr)[i]);
		}
		kprintf("\n");
#endif
	}
}
#endif
