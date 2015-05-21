/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2015 Dale Weiler
 *
 * Spin locks with waiters
 *
 */
#include <system.h>

static inline int arch_atomic_swap(volatile int * x, int v) {
	asm("xchg %0, %1" : "=r"(v), "=m"(*x) : "0"(v) : "memory");
	return v;
}

static inline void arch_atomic_store(volatile int * p, int x) {
	asm("movl %1, %0" : "=m"(*p) : "r"(x) : "memory");
}

static inline void arch_atomic_inc(volatile int * x) {
	asm("lock; incl %0" : "=m"(*x) : "m"(*x) : "memory");
}

static inline void arch_atomic_dec(volatile int * x) {
	asm("lock; decl %0" : "=m"(*x) : "m"(*x) : "memory");
}

void spin_wait(volatile int * addr, volatile int * waiters) {
	if (waiters) {
		arch_atomic_inc(waiters);
	}
	while (*addr) {
		switch_task(1);
	}
	if (waiters) {
		arch_atomic_dec(waiters);
	}
}

void spin_lock(spin_lock_t lock) {
	while (arch_atomic_swap(lock, 1)) {
		spin_wait(lock, lock+1);
	}
}

void spin_init(spin_lock_t lock) {
	lock[0] = 0;
	lock[1] = 0;
}

void spin_unlock(spin_lock_t lock) {
	if (lock[0]) {
		arch_atomic_store(lock, 0);
		if (lock[1])
			switch_task(1);
	}
}
