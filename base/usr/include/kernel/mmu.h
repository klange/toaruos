#pragma once
#include <stdint.h>

#if defined(__x86_64__)
#include <kernel/arch/x86_64/pml.h>
#elif defined(__aarch64__)
#include <kernel/arch/aarch64/pml.h>
#endif

#define KERNEL_HEAP_START 0xFFFFff0000000000UL
#define MMIO_BASE_START   0xffffff1fc0000000UL
#define HIGH_MAP_REGION   0xffffff8000000000UL
#define MODULE_BASE_START 0xffffffff80000000UL
#define USER_SHM_LOW      0x0000400100000000UL
#define USER_SHM_HIGH     0x0000500000000000UL
#define USER_DEVICE_MAP   0x0000400000000000UL

#define MMU_FLAG_KERNEL       0x01
#define MMU_FLAG_WRITABLE     0x02
#define MMU_FLAG_NOCACHE      0x04
#define MMU_FLAG_WRITETHROUGH 0x08
#define MMU_FLAG_SPEC         0x10
#define MMU_FLAG_WC           (MMU_FLAG_NOCACHE | MMU_FLAG_WRITETHROUGH | MMU_FLAG_SPEC)
#define MMU_FLAG_NOEXECUTE    0x20

#define MMU_GET_MAKE 0x01


#define MMU_PTR_NULL  1
#define MMU_PTR_WRITE 2

void mmu_frame_set(uintptr_t frame_addr);
void mmu_frame_clear(uintptr_t frame_addr);
void mmu_frame_release(uintptr_t frame_addr);
int mmu_frame_test(uintptr_t frame_addr);
uintptr_t mmu_first_n_frames(int n);
uintptr_t mmu_first_frame(void);
void mmu_frame_allocate(union PML * page, unsigned int flags);
void mmu_frame_map_address(union PML * page, unsigned int flags, uintptr_t physAddr);
void mmu_frame_free(union PML * page);
uintptr_t mmu_map_to_physical(union PML * root, uintptr_t virtAddr);
union PML * mmu_get_page(uintptr_t virtAddr, int flags);
void mmu_set_directory(union PML * new_pml);
void mmu_free(union PML * from);
union PML * mmu_clone(union PML * from);
void mmu_invalidate(uintptr_t addr);
uintptr_t mmu_allocate_a_frame(void);
uintptr_t mmu_allocate_n_frames(int n);
union PML * mmu_get_kernel_directory(void);
void * mmu_map_from_physical(uintptr_t frameaddress);
void * mmu_map_mmio_region(uintptr_t physical_address, size_t size);
void * mmu_map_module(size_t size);
void mmu_unmap_module(uintptr_t base_address, size_t size);

size_t mmu_count_user(union PML * from);
size_t mmu_count_shm(union PML * from);
size_t mmu_total_memory(void);
size_t mmu_used_memory(void);

void * sbrk(size_t);

union PML * mmu_get_page_other(union PML * root, uintptr_t virtAddr);
int mmu_validate_user_pointer(void * addr, size_t size, int flags);
