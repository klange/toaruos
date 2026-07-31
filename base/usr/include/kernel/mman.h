#pragma once

#include <stdint.h>
#include <kernel/types.h>

extern long mmap_sbrk(size_t size);
extern long mmap_anon(uintptr_t addr, size_t length, int prot, int flags);
extern long mmap_file(uintptr_t addr, size_t length, int prot, int flags, fs_node_t * file, off_t offset);
extern long mmap_unmap(uintptr_t addr, size_t length);

extern long generic_page_fault(uintptr_t addr, int flags, struct regs *);
extern long mmap_fault_other(process_t * proc, uintptr_t addr, int flags);

