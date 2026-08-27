#pragma once

#include <stdint.h>
#include <kernel/types.h>

#define MAP_SYSCALL      0x1000
#define MAP_FD_WRITABLE  0x2000

extern long mmap_sbrk(size_t size);
extern long do_mmap(uintptr_t addr, size_t length, int prot, int flags, fs_node_t * file, off_t offset);
extern long mmap_unmap(uintptr_t addr, size_t length);

enum fault_code {
	FAULT_CODE_FROM_KERNEL = 0x00000001,
	FAULT_CODE_READ        = 0x00000002,
	FAULT_CODE_WRITE       = 0x00000004,
	FAULT_CODE_INSTR       = 0x00000008,
};

enum fault_response {
	FAULT_RESPONSE_RESUME = 0,
	FAULT_RESPONSE_BAD_WRITE,
	FAULT_RESPONSE_BAD_READ,
	FAULT_RESPONSE_BAD_INSTR,
	FAULT_RESPONSE_NO_MAPPING,
};

extern enum fault_response generic_page_fault(uintptr_t addr, enum fault_code flags, struct regs *);
extern enum fault_response mmap_fault_other(process_t * proc, uintptr_t addr, enum fault_code flags);
extern int mmu_check_syscall_capability(process_t * proc, uintptr_t addr);

