#pragma once

#include <kernel/time.h>
#include <kernel/misc.h>
#include <kernel/printf.h>

typedef volatile struct {
    volatile int latch[1];
    int owner;
    const char * func;
} spin_lock_t;
#define spin_init(lock) do { (lock).owner = 0; (lock).latch[0] = 0; (lock).func = NULL; } while (0)

extern void arch_spin_lock_acquire(const char * name, spin_lock_t * lock, const char * func);
extern void arch_spin_lock_release(spin_lock_t * lock);
#define spin_lock(lock) arch_spin_lock_acquire(#lock, &lock, __func__)
#define spin_unlock(lock) arch_spin_lock_release(&lock)

