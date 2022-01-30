#pragma once
#include <kernel/types.h>

union PML {
    struct {
        uint64_t present:1;
        uint64_t writable:1;
        uint64_t user:1;
        uint64_t writethrough:1;
        uint64_t nocache:1;
        uint64_t accessed:1;
        uint64_t _available1:1;
        uint64_t size:1;
        uint64_t global:1;
        uint64_t cow_pending:1;
        uint64_t _available2:2;
        uint64_t page:28;
        uint64_t reserved:12;
        uint64_t _available3:11;
        uint64_t nx:1;
    } bits;
    uint64_t raw;
};

#define mmu_page_is_user_readable(p) (p->bits.user)
#define mmu_page_is_user_writable(p) (p->bits.writable)
