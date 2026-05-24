#include <kernel/printf.h>
#include <kernel/misc.h>

int __ubsan_enabled = 0;

#define __ubsan_prelude() do { \
	if (!__ubsan_enabled) return; \
	__ubsan_enabled = 0; \
} while (0)

#define __ubsan_epilogue() do { \
	__ubsan_enabled = 1; \
} while (0)

void __ubsan_handle_add_overflow(void *data, void *lhs, void *rhs) {
	__ubsan_prelude();
	dprintf("add_overflow\n");
	arch_dump_traceback();
	__ubsan_epilogue();
}

void __ubsan_handle_builtin_unreachable(void *data) {
	__ubsan_prelude();
	dprintf("builtin_unreachable\n");
	arch_dump_traceback();
	__ubsan_epilogue();
}

void __ubsan_handle_divrem_overflow(void *data, void *lhs, void *rhs) {
	__ubsan_prelude();
	dprintf("divrem_overflow\n");
	arch_dump_traceback();
	__ubsan_epilogue();
}

void __ubsan_handle_mul_overflow(void *data, void *lhs, void *rhs) {
	__ubsan_prelude();
	dprintf("mul_overflow\n");
	arch_dump_traceback();
	__ubsan_epilogue();
}

void __ubsan_handle_negate_overflow(void *data, void *lhs, void *rhs) {
	__ubsan_prelude();
	dprintf("negate_overflow\n");
	arch_dump_traceback();
	__ubsan_epilogue();
}

void __ubsan_handle_out_of_bounds(void *data, void *index) {
	__ubsan_prelude();
	dprintf("out_of_bounds\n");
	arch_dump_traceback();
	__ubsan_epilogue();
}

void __ubsan_handle_pointer_overflow(void *data, void *base, void *result) {
	__ubsan_prelude();
	dprintf("pointer_overflow\n");
	arch_dump_traceback();
	__ubsan_epilogue();
}

void __ubsan_handle_shift_out_of_bounds(void *data, void *lhs, void *rhs) {
	__ubsan_prelude();
	dprintf("shift_out_of_bounds\n");
	arch_dump_traceback();
	__ubsan_epilogue();
}

void __ubsan_handle_sub_overflow(void *data, void *lhs, void *rhs) {
	__ubsan_prelude();
	dprintf("sub_overflow\n");
	arch_dump_traceback();
	__ubsan_epilogue();
}

void __ubsan_handle_type_mismatch_v1(void *data, void *ptr) {
	__ubsan_prelude();
	dprintf("type_mismatch %p\n", ptr);
	arch_dump_traceback();
	__ubsan_epilogue();
}

void __ubsan_handle_vla_bound_not_positive(void *data, void *bound) {
	if ((uintptr_t)bound == 0) return;
	__ubsan_prelude();
	dprintf("vla_bound_not_positive %#zx\n", (uintptr_t)bound);
	arch_dump_traceback();
	__ubsan_epilogue();
}

