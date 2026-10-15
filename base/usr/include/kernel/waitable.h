#pragma once

#include <kernel/spinlock.h>
#include <kernel/etree.h>

struct waitable_waiter {
	struct elist list_node;
	int (*wake_func)(struct waitable_waiter*, void*);
	void * priv;
	void * key;
};

struct waitable_queue {
	spin_lock_t lock;
	struct elist queue;
};

static inline void waitable_waiter_init(struct waitable_waiter *waiter, int (*func)(struct waitable_waiter*, void*), void *priv) {
	elist_init(&waiter->list_node);
	waiter->wake_func = func;
	waiter->priv = priv;
	waiter->key = NULL;
}

static inline void waitable_queue_init(struct waitable_queue *queue) {
	spin_init(queue->lock);
	elist_init(&queue->queue);
}

extern int waitable_wake_generic(struct waitable_waiter *waiter, void *key);
extern int waitable_await_unlocking(struct waitable_queue *queue, spin_lock_t * lock);
extern int waitable_awaken_all(struct waitable_queue *queue, void *key);
extern int waitable_awaken_one(struct waitable_queue *queue, void *key);

