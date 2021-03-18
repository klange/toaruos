#pragma once
/**
 * @file threads.h
 * @brief Convience header for providing atomic operations to threads.
 */

#ifdef ENABLE_THREADING
#include <pthread.h>
#include <sched.h>

#ifdef _WIN32
#include <windows.h>
#include <winnt.h>
#define sched_yield() YieldProcessor()
#endif

static inline void _krk_internal_spin_lock(int volatile * lock) {
	while(__sync_lock_test_and_set(lock, 0x01)) {
		sched_yield();
	}
}

static inline void _krk_internal_spin_unlock(int volatile * lock) {
	__sync_lock_release(lock);
}

#define _obtain_lock(v)  _krk_internal_spin_lock(&v);
#define _release_lock(v) _krk_internal_spin_unlock(&v);

#else

#define _obtain_lock(v)
#define _release_lock(v)

#define pthread_rwlock_init(a,b)
#define pthread_rwlock_wrlock(a)
#define pthread_rwlock_rdlock(a)
#define pthread_rwlock_unlock(a)

#endif

