#ifndef _SPINLOCK_H_
#define _SPINLOCK_H_

#ifndef spin_lock
static void spin_lock(int volatile * lock) {
	while(__sync_lock_test_and_set(lock, 0x01)) {
		syscall_yield();
	}
}

static void spin_unlock(int volatile * lock) {
	__sync_lock_release(lock);
}
#endif

#endif /* _SPINLOCK_H */
