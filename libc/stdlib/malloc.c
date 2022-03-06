/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * klange's Slab Allocator
 *
 * Implemented for CS241, Fall 2010, machine problem 7
 * at the University of Illinois, Urbana-Champaign.
 *
 * Overall competition winner for speed.
 * Well ranked in memory usage.
 *
 * Copyright (c) 2010-2018 K. Lange.  All rights reserved.
 *
 * Developed by: K. Lange <klange@toaruos.org>
 *               Dave Majnemer <dmajnem2@acm.uiuc.edu>
 *               Assocation for Computing Machinery
 *               University of Illinois, Urbana-Champaign
 *               http://acm.uiuc.edu
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal with the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *   1. Redistributions of source code must retain the above copyright notice,
 *      this list of conditions and the following disclaimers.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimers in the
 *      documentation and/or other materials provided with the distribution.
 *   3. Neither the names of the Association for Computing Machinery, the
 *      University of Illinois, nor the names of its contributors may be used
 *      to endorse or promote products derived from this Software without
 *      specific prior written permission.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * WITH THE SOFTWARE.
 *
 * ##########
 * # README #
 * ##########
 *
 * About the slab allocator
 * """"""""""""""""""""""""
 *
 * This is a simple implementation of a "slab" allocator. It works by operating
 * on "bins" of items of predefined sizes and a set of pseudo-bins of any size.
 * When a new allocation request is made, the allocator determines if it will
 * fit in an existing bin. If there are no bins of the correct size for a given
 * allocation request, the allocator will make a bin and add it to a(n empty)
 * list of available bins of that size. In this implementation, we use sizes
 * from 4 bytes (32 bit) or 8 bytes (64-bit) to 2KB for bins, fitting a 4K page
 * size. The implementation allows the number of pages in a single bin to be
 * increased, as well as allowing for changing the size of page (though this
 * should, for the most part, remain 4KB under any modern system).
 *
 * Special thanks
 * """"""""""""""
 *
 * I would like to thank Dave Majnemer, who I have credited above as a
 * contributor, for his assistance. Without Dave, klmalloc would be a mash
 * up of bits of forward movement in no discernible pattern. Dave helped
 * me ensure that I could build a proper slab allocator and has consantly
 * derided me for not fixing the bugs and to-do items listed in the last
 * section of this readme.
 *
 * GCC Function Attributes
 * """""""""""""""""""""""
 *
 * A couple of GCC function attributes, designated by the __attribute__
 * directive, are used in this code to streamline optimization.
 * I've chosen to include a brief overview of the particular attributes
 * I am making use of:
 *
 * - malloc:
 *   Tells gcc that a given function is a memory allocator
 *   and that non-NULL values it returns should never be
 *   associated with other chunks of memory. We use this for
 *   alloc, realloc and calloc, as is requested in the gcc
 *   documentation for the attribute.
 *
 * - always_inline:
 *   Tells gcc to always inline the given code, regardless of the
 *   optmization level. Small functions that would be noticeably
 *   slower with the overhead of paramter handling are given
 *   this attribute.
 *
 * - pure:
 *   Tells gcc that a function only uses inputs and its output.
 *
 * Things to work on
 * """""""""""""""""
 *
 * TODO: Try to be more consistent on comment widths...
 * FIXME: Make thread safe! Not necessary for competition, but would be nice.
 * FIXME: Splitting/coalescing is broken. Fix this ASAP!
 *
**/

/* Includes {{{ */
#include <syscall.h>
#include <assert.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>
/* }}} */
/* Definitions {{{ */

#define sbrk syscall_sbrk

/*
 * Defines for often-used integral values
 * related to our binning and paging strategy.
 */
#if defined(__x86_64__) || defined(__aarch64__)
#define NUM_BINS 10U								/* Number of bins, total, under 64-bit. */
#define SMALLEST_BIN_LOG 3U							/* Logarithm base two of the smallest bin: log_2(sizeof(int32)). */
#else
#define NUM_BINS 11U								/* Number of bins, total, under 32-bit. */
#define SMALLEST_BIN_LOG 2U							/* Logarithm base two of the smallest bin: log_2(sizeof(int32)). */
#endif
#define BIG_BIN (NUM_BINS - 1)						/* Index for the big bin, (NUM_BINS - 1) */
#define SMALLEST_BIN (1UL << SMALLEST_BIN_LOG)		/* Size of the smallest bin. */

#define PAGE_SIZE 0x1000							/* Size of a page (in bytes), should be 4KB */
#define PAGE_MASK (PAGE_SIZE - 1)					/* Block mask, size of a page * number of pages - 1. */
#define SKIP_P INT32_MAX							/* INT32_MAX is half of UINT32_MAX; this gives us a 50% marker for skip lists. */
#define SKIP_MAX_LEVEL 6							/* We have a maximum of 6 levels in our skip lists. */

#define BIN_MAGIC 0xDEFAD00D

/* }}} */

/*
 * Internal functions.
 */
static void * __attribute__ ((malloc)) klmalloc(uintptr_t size);
static void * __attribute__ ((malloc)) klrealloc(void * ptr, uintptr_t size);
static void * __attribute__ ((malloc)) klcalloc(uintptr_t nmemb, uintptr_t size);
static void * __attribute__ ((malloc)) klvalloc(uintptr_t size);
static void klfree(void * ptr);

static int volatile mem_lock = 0;
static const char * _lock_holder;

#ifdef assert
#undef assert
#define assert(statement) ((statement) ? (void)0 : _malloc_assert(__FILE__, __LINE__, __FUNCTION__, #statement))
#endif

#define WRITE(x) syscall_write(2, (char*)x, sizeof(x))
#define WRITEV(x) syscall_write(2, (char*)x, strlen(x))
static void _malloc_assert(const char * file, int line, const char * func, const char *x) {
	WRITEV(func);
	WRITE(" in ");
	WRITEV(file);
	WRITE(" failed assertion: ");
	WRITEV(x);
	WRITE("\n");
	exit(1);
}

extern int __libc_is_multicore;

static inline void _yield(void) {
	if (!__libc_is_multicore) syscall_yield();
}

static void spin_lock(int volatile * lock, const char * caller) {
	while(__sync_lock_test_and_set(lock, 0x01)) {
		_yield();
	}
	_lock_holder = caller;
}

static void spin_unlock(int volatile * lock) {
	__sync_lock_release(lock);
}


void * __attribute__ ((malloc)) malloc(uintptr_t size) {
	spin_lock(&mem_lock, __FUNCTION__);
	void * ret = klmalloc(size);
	spin_unlock(&mem_lock);
	return ret;
}

void * __attribute__ ((malloc)) realloc(void * ptr, uintptr_t size) {
	spin_lock(&mem_lock, __FUNCTION__);
	void * ret = klrealloc(ptr, size);
	spin_unlock(&mem_lock);
	return ret;
}

void * __attribute__ ((malloc)) calloc(uintptr_t nmemb, uintptr_t size) {
	spin_lock(&mem_lock, __FUNCTION__);
	void * ret = klcalloc(nmemb, size);
	spin_unlock(&mem_lock);
	return ret;
}

void * __attribute__ ((malloc)) valloc(uintptr_t size) {
	spin_lock(&mem_lock, __FUNCTION__);
	void * ret = klvalloc(size);
	spin_unlock(&mem_lock);
	return ret;
}

void free(void * ptr) {
	spin_lock(&mem_lock, __FUNCTION__);
	klfree(ptr);
	spin_unlock(&mem_lock);
}


/* Bin management {{{ */

/*
 * Adjust bin size in bin_size call to proper bounds.
 */
static inline uintptr_t __attribute__ ((always_inline, pure)) klmalloc_adjust_bin(uintptr_t bin)
{
	if (bin <= (uintptr_t)SMALLEST_BIN_LOG)
	{
		return 0;
	}
	bin -= SMALLEST_BIN_LOG + 1;
	if (bin > (uintptr_t)BIG_BIN) {
		return BIG_BIN;
	}
	return bin;
}

/*
 * Given a size value, find the correct bin
 * to place the requested allocation in.
 */
static inline uintptr_t __attribute__ ((always_inline, pure)) klmalloc_bin_size(uintptr_t size) {
	uintptr_t bin = sizeof(size) * CHAR_BIT - __builtin_clzl(size);
	bin += !!(size & (size - 1));
	return klmalloc_adjust_bin(bin);
}

/*
 * Bin header - One page of memory.
 * Appears at the front of a bin to point to the
 * previous bin (or NULL if the first), the next bin
 * (or NULL if the last) and the head of the bin, which
 * is a stack of cells of data.
 */
typedef struct _klmalloc_bin_header {
	struct _klmalloc_bin_header *  next;	/* Pointer to the next node. */
	void * head;							/* Head of this bin. */
	uintptr_t size;							/* Size of this bin, if big; otherwise bin index. */
	uint32_t bin_magic;
} klmalloc_bin_header;

/*
 * A big bin header is basically the same as a regular bin header
 * only with a pointer to the previous (physically) instead of
 * a "next" and with a list of forward headers.
 */
typedef struct _klmalloc_big_bin_header {
	struct _klmalloc_big_bin_header * next;
	void * head;
	uintptr_t size;
	uint32_t bin_magic;
} klmalloc_big_bin_header;


/*
 * List of pages in a bin.
 */
typedef struct _klmalloc_bin_header_head {
	klmalloc_bin_header * first;
} klmalloc_bin_header_head;

/*
 * Array of available bins.
 */
static klmalloc_bin_header_head klmalloc_bin_head[NUM_BINS - 1];	/* Small bins */

/* }}} Bin management */
/* Doubly-Linked List {{{ */

/*
 * Remove an entry from a page list.
 * Decouples the element from its
 * position in the list by linking
 * its neighbors to eachother.
 */
static inline void __attribute__ ((always_inline)) klmalloc_list_decouple(klmalloc_bin_header_head *head, klmalloc_bin_header *node) {
	klmalloc_bin_header *next	= node->next;
	head->first = next;
	node->next = NULL;
}

/*
 * Insert an entry into a page list.
 * The new entry is placed at the front
 * of the list and the existing border
 * elements are updated to point back
 * to it (our list is doubly linked).
 */
static inline void __attribute__ ((always_inline)) klmalloc_list_insert(klmalloc_bin_header_head *head, klmalloc_bin_header *node) {
	node->next = head->first;
	head->first = node;
}

/*
 * Get the head of a page list.
 * Because redundant function calls
 * are really great, and just in case
 * we change the list implementation.
 */
static inline klmalloc_bin_header * __attribute__ ((always_inline)) klmalloc_list_head(klmalloc_bin_header_head *head) {
	return head->first;
}

/* }}} Lists */
/* Stack {{{ */
/*
 * Pop an item from a block.
 * Free space is stored as a stack,
 * so we get a free space for a bin
 * by popping a free node from the
 * top of the stack.
 */
static void * klmalloc_stack_pop(klmalloc_bin_header *header) {
	assert(header);
	assert(header->head != NULL);
	assert((uintptr_t)header->head > (uintptr_t)header);
	if (header->size > NUM_BINS) {
		assert((uintptr_t)header->head < (uintptr_t)header + header->size);
	} else {
		assert((uintptr_t)header->head < (uintptr_t)header + PAGE_SIZE);
		assert((uintptr_t)header->head > (uintptr_t)header + sizeof(klmalloc_bin_header) - 1);
	}
	
	/*
	 * Remove the current head and point
	 * the head to where the old head pointed.
	 */
	void *item = header->head;
	uintptr_t **head = header->head;
	uintptr_t *next = *head;
	header->head = next;
	return item;
}

/*
 * Push an item into a block.
 * When we free memory, we need
 * to add the freed cell back
 * into the stack of free spaces
 * for the block.
 */
static void klmalloc_stack_push(klmalloc_bin_header *header, void *ptr) {
	assert(ptr != NULL);
	assert((uintptr_t)ptr > (uintptr_t)header);
	if (header->size > NUM_BINS) {
		assert((uintptr_t)ptr < (uintptr_t)header + header->size);
	} else {
		assert((uintptr_t)ptr < (uintptr_t)header + PAGE_SIZE);
	}
	uintptr_t **item = (uintptr_t **)ptr;
	*item = (uintptr_t *)header->head;
	header->head = item;
}

/*
 * Is this cell stack empty?
 * If the head of the stack points
 * to NULL, we have exhausted the
 * stack, so there is no more free
 * space available in the block.
 */
static inline int __attribute__ ((always_inline)) klmalloc_stack_empty(klmalloc_bin_header *header) {
	return header->head == NULL;
}

/* }}} Stack */

/* malloc() {{{ */
static void * __attribute__ ((malloc)) klmalloc(uintptr_t size) {
	/*
	 * C standard implementation:
	 * If size is zero, we can choose do a number of things.
	 * This implementation will return a NULL pointer.
	 */
	if (__builtin_expect(size == 0, 0))
		return NULL;

	/*
	 * Find the appropriate bin for the requested
	 * allocation and start looking through that list.
	 */
	unsigned int bucket_id = klmalloc_bin_size(size);

	if (bucket_id < BIG_BIN) {
		/*
		 * Small bins.
		 */
		klmalloc_bin_header * bin_header = klmalloc_list_head(&klmalloc_bin_head[bucket_id]);
		if (!bin_header) {
			/*
			 * Grow the heap for the new bin.
			 */
			bin_header = (klmalloc_bin_header*)sbrk(PAGE_SIZE);
			bin_header->bin_magic = BIN_MAGIC;
			assert((uintptr_t)bin_header % PAGE_SIZE == 0);

			/*
			 * Set the head of the stack.
			 */
			bin_header->head = (void*)((uintptr_t)bin_header + sizeof(klmalloc_bin_header));
			/*
			 * Insert the new bin at the front of
			 * the list of bins for this size.
			 */
			klmalloc_list_insert(&klmalloc_bin_head[bucket_id], bin_header);
			/*
			 * Initialize the stack inside the bin.
			 * The stack is initially full, with each
			 * entry pointing to the next until the end
			 * which points to NULL.
			 */
			uintptr_t adj = SMALLEST_BIN_LOG + bucket_id;
			uintptr_t i, available = ((PAGE_SIZE - sizeof(klmalloc_bin_header)) >> adj) - 1;

			uintptr_t **base = bin_header->head;
			for (i = 0; i < available; ++i) {
				/*
				 * Our available memory is made into a stack, with each
				 * piece of memory turned into a pointer to the next
				 * available piece. When we want to get a new piece
				 * of memory from this block, we just pop off a free
				 * spot and give its address.
				 */
				base[i << bucket_id] = (uintptr_t *)&base[(i + 1) << bucket_id];
			}
			base[available << bucket_id] = NULL;
			bin_header->size = bucket_id;
		}
		uintptr_t ** item = klmalloc_stack_pop(bin_header);
		if (klmalloc_stack_empty(bin_header)) {
			klmalloc_list_decouple(&(klmalloc_bin_head[bucket_id]),bin_header);
		}
		return item;
	} else {
		/*
		 * Round requested size to a set of pages, plus the header size.
		 */
		uintptr_t pages = (size + sizeof(klmalloc_big_bin_header)) / PAGE_SIZE + 1;
		klmalloc_big_bin_header * bin_header = (klmalloc_big_bin_header*)sbrk(PAGE_SIZE * pages);
		bin_header->bin_magic = BIN_MAGIC;
		assert((uintptr_t)bin_header % PAGE_SIZE == 0);
		/*
		 * Give the header the remaining space.
		 */
		bin_header->size = pages * PAGE_SIZE - sizeof(klmalloc_big_bin_header);
		assert((bin_header->size + sizeof(klmalloc_big_bin_header)) % PAGE_SIZE == 0);
		/*
		 * Return the head of the block.
		 */
		bin_header->head = NULL;
		return (void*)((uintptr_t)bin_header + sizeof(klmalloc_big_bin_header));
	}
}
/* }}} */
/* free() {{{ */
static void klfree(void *ptr) {
	/*
	 * C standard implementation: Do nothing when NULL is passed to free.
	 */
	if (__builtin_expect(ptr == NULL, 0)) {
		return;
	}

	/*
	 * Woah, woah, hold on, was this a page-aligned block?
	 */
	if ((uintptr_t)ptr % PAGE_SIZE == 0) {
		/*
		 * Well howdy-do, it was.
		 */
		ptr = (void *)((uintptr_t)ptr - 1);
	}

	/*
	 * Get our pointer to the head of this block by
	 * page aligning it.
	 */
	klmalloc_bin_header * header = (klmalloc_bin_header *)((uintptr_t)ptr & (uintptr_t)~PAGE_MASK);
	assert((uintptr_t)header % PAGE_SIZE == 0);

	if (header->bin_magic != BIN_MAGIC)
		return;

	/*
	 * For small bins, the bin number is stored in the size
	 * field of the header. For large bins, the actual size
	 * available in the bin is stored in this field. It's
	 * easy to tell which is which, though.
	 */
	uintptr_t bucket_id = header->size;
	if (bucket_id > (uintptr_t)NUM_BINS) {
		bucket_id = BIG_BIN;
		klmalloc_big_bin_header *bheader = (klmalloc_big_bin_header*)header;
		
		assert(bheader);
		assert(bheader->head == NULL);
		assert((bheader->size + sizeof(klmalloc_big_bin_header)) % PAGE_SIZE == 0);

		char * args[] = {(char*)header, (char*)(bheader->size + sizeof(klmalloc_big_bin_header))};
		syscall_sysfunc(43, args);
	} else {
		/*
		 * If the stack is empty, we are freeing
		 * a block from a previously full bin.
		 * Return it to the busy bins list.
		 */
		if (klmalloc_stack_empty(header)) {
			klmalloc_list_insert(&klmalloc_bin_head[bucket_id], header);
		}
		/*
		 * Push new space back into the stack.
		 */
		klmalloc_stack_push(header, ptr);
	}
}
/* }}} */
/* valloc() {{{ */
static void * __attribute__ ((malloc)) klvalloc(uintptr_t size) {
	/*
	 * Allocate a page-aligned block.
	 * XXX: THIS IS HORRIBLY, HORRIBLY WASTEFUL!! ONLY USE THIS
	 *      IF YOU KNOW WHAT YOU ARE DOING!
	 */
	uintptr_t true_size = size + PAGE_SIZE - sizeof(klmalloc_big_bin_header); /* Here we go... */
	void * result = klmalloc(true_size);
	void * out = (void *)((uintptr_t)result + (PAGE_SIZE - sizeof(klmalloc_big_bin_header)));
	assert((uintptr_t)out % PAGE_SIZE == 0);
	return out;
}
/* }}} */
/* realloc() {{{ */
static void * __attribute__ ((malloc)) klrealloc(void *ptr, uintptr_t size) {
	/*
	 * C standard implementation: When NULL is passed to realloc,
	 * simply malloc the requested size and return a pointer to that.
	 */
	if (__builtin_expect(ptr == NULL, 0))
		return klmalloc(size);

	/*
	 * C standard implementation: For a size of zero, free the
	 * pointer and return NULL, allocating no new memory.
	 */
	if (__builtin_expect(size == 0, 0))
	{
		free(ptr);
		return NULL;
	}

	/*
	 * Find the bin for the given pointer
	 * by aligning it to a page.
	 */
	klmalloc_bin_header * header_old = (void *)((uintptr_t)ptr & (uintptr_t)~PAGE_MASK);
	if (header_old->bin_magic != BIN_MAGIC) {
		assert(0 && "Bad magic on realloc.");
		return NULL;
	}

	uintptr_t old_size = header_old->size;
	if (old_size < (uintptr_t)BIG_BIN) {
		/*
		 * If we are copying from a small bin,
		 * we need to get the size of the bin
		 * from its id.
		 */
		old_size = (1UL << (SMALLEST_BIN_LOG + old_size));
	}

	if (old_size == size) return ptr;

	/*
	 * Reallocate more memory.
	 */
	void * newptr = klmalloc(size);
	if (__builtin_expect(newptr != NULL, 1)) {

		/*
		 * Copy the old value into the new value.
		 * Be sure to only copy as much as was in
		 * the old block.
		 */
		memcpy(newptr, ptr, (old_size < size) ? old_size : size);
		klfree(ptr);
		return newptr;
	}

	/*
	 * We failed to allocate more memory,
	 * which means we're probably out.
	 *
	 * Bail and return NULL.
	 */
	return NULL;
}
/* }}} */
/* calloc() {{{ */
static void * __attribute__ ((malloc)) klcalloc(uintptr_t nmemb, uintptr_t size) {
	/*
	 * Allocate memory and zero it before returning
	 * a pointer to the newly allocated memory.
	 * 
	 * Implemented by way of a simple malloc followed
	 * by a memset to 0x00 across the length of the
	 * requested memory chunk.
	 */

	void *ptr = klmalloc(nmemb * size);
	if (ptr) memset(ptr,0x00,nmemb * size);
	return ptr;
}
/* }}} */


