/**
 * @file kernel/misc/waitable.c
 * @brief Wait queue functions.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2026 K. Lange
 */
#include <kernel/process.h>
#include <kernel/waitable.h>

int waitable_wake_generic(struct waitable_waiter *waiter, void *key) {
	waiter->key = key;
	make_process_ready(waiter->priv);
	return 0;
}

int waitable_await_unlocking(struct waitable_queue *queue, spin_lock_t * lock) {
	struct waitable_waiter w;
	waitable_waiter_init(&w, waitable_wake_generic, this_core->current_process);
	spin_lock(queue->lock);
	elist_insert_tail(&queue->queue, &w.list_node);
	spin_unlock(queue->lock);
	spin_unlock(*lock);
	//dprintf("wait on generic awaitable in %d (%s)\n", this_core->current_process->id, this_core->current_process->name);
	switch_task(0);
	spin_lock(queue->lock);
	__elist_del(&w.list_node);
	spin_unlock(queue->lock);
	return (w.key == NULL);
}

static inline struct waitable_waiter * waiter_from_queue_entry(struct elist *ent) {
	return (void*)((char*)ent - offsetof(struct waitable_waiter, list_node));
}

int waitable_awaken_all(struct waitable_queue *queue, void *key) {
	int woken = 0;
	/* Call functions but do not remove elements, that happens elsewhere */
	spin_lock(queue->lock);
	for (struct elist *ent = queue->queue.el_next; ent != &queue->queue;) {
		struct waitable_waiter * w = waiter_from_queue_entry(ent);
		struct elist *next = ent->el_next;
		if (!w->wake_func(w, key)) {
			__elist_del(ent);
			woken++;
		}
		ent = next;
	}
	spin_unlock(queue->lock);
	return woken++;
}

int waitable_awaken_one(struct waitable_queue *queue, void *key) {
	/* Call functions but do not remove elements, that happens elsewhere */
	spin_lock(queue->lock);
	for (struct elist *ent = queue->queue.el_next; ent != &queue->queue;) {
		struct waitable_waiter * w = waiter_from_queue_entry(ent);
		struct elist *next = ent->el_next;
		if (!w->wake_func(w, key)) {
			__elist_del(ent);
			spin_unlock(queue->lock);
			return 1;
		}
		ent = next;
	}
	spin_unlock(queue->lock);
	return 0;
}
