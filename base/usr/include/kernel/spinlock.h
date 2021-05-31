#pragma once

typedef volatile struct {
    volatile int latch[1];
    int owner;
    const char * func;
} spin_lock_t;
#define spin_init(lock) do { (lock).owner = 0; (lock).latch[0] = 0; (lock).func = NULL; } while (0)

#define DEBUG_LOCKS
#ifdef DEBUG_LOCKS
#define spin_lock(lock) do { while (__sync_lock_test_and_set((lock).latch, 0x01)); (lock).owner = this_core->cpu_id+1; (lock).func = __func__; } while (0)
#define spin_unlock(lock) do { (lock).func = NULL; (lock).owner = -1; __sync_lock_release((lock).latch); } while (0)
#else
#define spin_lock(lock) do { while (__sync_lock_test_and_set((lock).latch, 0x01)); } while(0)
#define spin_unlock(lock) __sync_lock_release((lock).latch);
#endif

#include <kernel/process.h>
