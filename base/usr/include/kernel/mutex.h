/**
 * Mutex that sleeps... and can be owned across sleeping...
 *
 * @copyright 2014-2021 K. Lange <klange@toaruos.org>
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 */
#include <kernel/list.h>
#include <kernel/spinlock.h>
#include <kernel/process.h>

typedef struct {
	spin_lock_t inner_lock;
	volatile int status;
	process_t * owner;
	list_t * waiters;
} sched_mutex_t;

extern sched_mutex_t * mutex_init(const char * name);
extern int mutex_acquire(sched_mutex_t * mutex);
extern int mutex_release(sched_mutex_t * mutex);
