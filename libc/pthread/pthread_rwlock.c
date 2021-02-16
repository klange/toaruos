#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <syscall.h>
#include <syscall_nums.h>
#include <signal.h>
#include <pthread.h>
#include <errno.h>

#include <sys/wait.h>

#define ACQUIRE_LOCK() do { while (__sync_lock_test_and_set(&lock->atomic_lock, 0x01)) { syscall_yield(); } } while (0)
#define RELEASE_LOCK() do { __sync_lock_release(&lock->atomic_lock); } while (0)

int pthread_rwlock_init(pthread_rwlock_t * lock, void * args) {
	lock->readers = 0;
	lock->atomic_lock = 0;
	if (args != NULL) {
		fprintf(stderr, "pthread: pthread_rwlock_init arg unsupported\n");
		return 1;
	}
	return 0;
}

int pthread_rwlock_wrlock(pthread_rwlock_t * lock) {
	ACQUIRE_LOCK();
	while (1) {
		if (lock->readers == 0) {
			lock->readers = -1;
			lock->writerPid = syscall_getpid();
			RELEASE_LOCK();
			return 0;
		}
		syscall_yield();
	}
}

int pthread_rwlock_rdlock(pthread_rwlock_t * lock) {
	ACQUIRE_LOCK();
	while (1) {
		if (lock->readers >= 0) {
			lock->readers++;
			RELEASE_LOCK();
			return 0;
		}
		syscall_yield();
	}
}

int pthread_rwlock_unlock(pthread_rwlock_t * lock) {
	ACQUIRE_LOCK();
	if (lock->readers > 0) lock->readers--;
	else if (lock->readers < 0) lock->readers = 0;
	else fprintf(stderr, "pthread: bad lock state detected\n");
	RELEASE_LOCK();
	return 0;
}

int pthread_rwlock_destroy(pthread_rwlock_t * lock) {
	return 0;
}
