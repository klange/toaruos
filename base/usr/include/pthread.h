#pragma once

#include <_cheader.h>
#include <stdint.h>
#include <time.h>

_Begin_C_Header

typedef struct {
	uint32_t id;
	char * stack;
	void * ret_val;
} pthread_t;
typedef unsigned int pthread_attr_t;

typedef struct {
	int volatile atomic_lock;
	int volatile readers;
	int writerPid;
} pthread_rwlock_t;

extern int pthread_create(pthread_t * thread, pthread_attr_t * attr, void *(*start_routine)(void *), void * arg);
extern void pthread_exit(void * value);
extern int pthread_kill(pthread_t thread, int sig);

extern int clone(uintptr_t,uintptr_t,void*);
extern int gettid();

extern void pthread_cleanup_push(void (*routine)(void *), void *arg);
extern void pthread_cleanup_pop(int execute);

typedef int volatile pthread_mutex_t;
typedef struct {
	volatile int waiters;
	volatile int condition;
	volatile int safety;
	volatile int wakeup;
} pthread_cond_t;
typedef int pthread_mutexattr_t;
typedef size_t pthread_key_t;
typedef int pthread_condattr_t;

extern int pthread_join(pthread_t thread, void **retval);

#define PTHREAD_MUTEX_INITIALIZER 0
#define PTHREAD_COND_INITIALIZER {0,0,0,0}

extern int pthread_cond_init(pthread_cond_t * cond, pthread_condattr_t *cond_attr);
extern int pthread_cond_signal(pthread_cond_t * cond);
extern int pthread_cond_broadcast(pthread_cond_t * cond);
extern int pthread_cond_wait(pthread_cond_t *restrict cond, pthread_mutex_t *restrict mutex);
extern int pthread_cond_timedwait(pthread_cond_t *restrict cond, pthread_mutex_t *restrict mutex, const struct timespec *restrict ts);
extern int pthread_cond_destroy(pthread_cond_t * cond);

extern int pthread_mutex_lock(pthread_mutex_t *mutex);
extern int pthread_mutex_trylock(pthread_mutex_t *mutex);
extern int pthread_mutex_unlock(pthread_mutex_t *mutex);
extern int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr);
extern int pthread_mutex_destroy(pthread_mutex_t *mutex);

extern int pthread_attr_init(pthread_attr_t *attr);
extern int pthread_attr_destroy(pthread_attr_t *attr);

extern int pthread_rwlock_init(pthread_rwlock_t * lock, void * args);
extern int pthread_rwlock_wrlock(pthread_rwlock_t * lock);
extern int pthread_rwlock_rdlock(pthread_rwlock_t * lock);
extern int pthread_rwlock_unlock(pthread_rwlock_t * lock);
extern int pthread_rwlock_destroy(pthread_rwlock_t * lock);

extern int pthread_key_create(pthread_key_t *key, void (*destructor)(void*));
extern int pthread_key_delete(pthread_key_t key);
extern void * pthread_getspecific(pthread_key_t key);
extern int pthread_setspecific(pthread_key_t key, const void * value);

extern int pthread_detach(pthread_t thread);
extern pthread_t pthread_self(void);

_End_C_Header
