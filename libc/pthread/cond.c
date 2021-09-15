#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <syscall.h>
#include <time.h>
#include <sys/times.h>

extern int __libc_is_multicore;
static inline void _yield(void) {
	if (!__libc_is_multicore) syscall_yield();
}

/* Yes, these are being implemented with spinlocks. Yes this is terrible. */
#define ACQUIRE_LOCK() do { while (__sync_lock_test_and_set(&cond->safety, 0x01)) { _yield(); } } while (0)
#define RELEASE_LOCK() do { __sync_lock_release(&cond->safety); } while (0)

int pthread_cond_init(pthread_cond_t * cond, pthread_condattr_t *cond_attr) {
	(void)cond_attr;
	cond->waiters = 0;
	cond->condition = 0;
	cond->safety = 0;
	cond->wakeup = 0;
	return 0;
}

int pthread_cond_signal(pthread_cond_t * cond) {
	ACQUIRE_LOCK();
	cond->condition = 1;
	if (cond->waiters) {
		cond->wakeup++;
	}
	RELEASE_LOCK();
	return 0;
}

int pthread_cond_broadcast(pthread_cond_t * cond) {
	ACQUIRE_LOCK();
	cond->condition = 1;
	for (int i = 0; i < cond->waiters; ++i) {
		cond->wakeup++;
	}
	RELEASE_LOCK();
	return 0;
}

int pthread_cond_wait(pthread_cond_t *restrict cond, pthread_mutex_t *restrict mutex) {
	ACQUIRE_LOCK();
	pthread_mutex_unlock(mutex);
	cond->waiters++;
	RELEASE_LOCK();
	while (!cond->condition) {
		ACQUIRE_LOCK();
		if (cond->wakeup) {
			cond->wakeup--;
			if (cond->condition) {
				if (cond->wakeup == 0) cond->condition = 0;
				RELEASE_LOCK();
				break;
			}
		}
		RELEASE_LOCK();
	}
	pthread_mutex_lock(mutex);
	return 0;
}

int pthread_cond_timedwait(pthread_cond_t *restrict cond, pthread_mutex_t *restrict mutex, const struct timespec *restrict ts) {
	clock_t target = times(NULL) + (ts->tv_sec * 1000UL + ts->tv_nsec / 1000UL);
	ACQUIRE_LOCK();
	pthread_mutex_unlock(mutex);
	cond->waiters++;
	RELEASE_LOCK();
	while (!cond->condition) {
		ACQUIRE_LOCK();
		if (cond->wakeup) {
			cond->wakeup--;
			if (cond->condition == 1) {
				if (cond->wakeup == 0) cond->condition = 0;
				RELEASE_LOCK();
				break;
			}
		}
		RELEASE_LOCK();
		if (target <= times(NULL)) {
			pthread_mutex_lock(mutex);
			return ETIMEDOUT;
		}
	}
	pthread_mutex_lock(mutex);
	return 0;
}

int pthread_cond_destroy(pthread_cond_t * cond) {
	return pthread_cond_init(cond, NULL);
}



