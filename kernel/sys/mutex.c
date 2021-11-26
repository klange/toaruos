/**
 * Mutex that sleeps... and can be owned across sleeping...
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange <klange@toaruos.org>
 */
#include <kernel/types.h>
#include <kernel/printf.h>
#include <kernel/time.h>
#include <kernel/string.h>
#include <kernel/spinlock.h>
#include <kernel/assert.h>
#include <kernel/process.h>
#include <kernel/list.h>
#include <kernel/mutex.h>

extern int wakeup_queue_one(list_t * queue);

sched_mutex_t * mutex_init(const char * name) {
	sched_mutex_t * out = malloc(sizeof(sched_mutex_t));
	spin_init(out->inner_lock);
	out->status = 0;
	out->owner = NULL;
	out->waiters = list_create(name, out);

	return out;
}

int mutex_acquire(sched_mutex_t * mutex) {
	spin_lock(mutex->inner_lock);
	while (mutex->status) {
		sleep_on_unlocking(mutex->waiters, &mutex->inner_lock);
		spin_lock(mutex->inner_lock);
	}
	mutex->status = 1;
	mutex->owner  = (process_t*)this_core->current_process;
	spin_unlock(mutex->inner_lock);
	return 0;
}

int mutex_release(sched_mutex_t * mutex) {
	assert(mutex->owner == this_core->current_process);
	spin_lock(mutex->inner_lock);
	mutex->owner  = NULL;
	mutex->status = 0;
	wakeup_queue_one(mutex->waiters);
	spin_unlock(mutex->inner_lock);

	return 0;
}
