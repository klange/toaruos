#include <kernel/system.h>
#include <kernel/types.h>
#include <kernel/fs.h>
#include <kernel/printf.h>
#include <kernel/ubsan.h>

#include <va_list.h>

#define EARLY_LOG_DEVICE 0x3F8
static uint32_t _ubsan_log_write(fs_node_t *node, uint64_t offset, uint32_t size, uint8_t *buffer) {
	for (unsigned int i = 0; i < size; ++i) {
		outportb(EARLY_LOG_DEVICE, buffer[i]);
	}
	return size;
}
static fs_node_t _ubsan_log = { .write = &_ubsan_log_write };

void ubsan_debug(struct SourceLocation * location) {
	fprintf(&_ubsan_log, "[ubsan] %s:%d:%dc - ", location->file_name, location->line, location->column);
}

void __ubsan_handle_add_overflow(struct OverflowData * data, unsigned long lhs, unsigned long rhs) {
	ubsan_debug(&data->location);
	fprintf(&_ubsan_log, "Overflow in add: %d %d\n", lhs, rhs);
}

void __ubsan_handle_sub_overflow(struct OverflowData * data, unsigned long lhs, unsigned long rhs) {
	ubsan_debug(&data->location);
	fprintf(&_ubsan_log, "Overflow in sub: %d %d\n", lhs, rhs);
}

void __ubsan_handle_mul_overflow(struct OverflowData * data, unsigned long lhs, unsigned long rhs) {
	ubsan_debug(&data->location);
	fprintf(&_ubsan_log, "Overflow in mul: %d %d\n", lhs, rhs);
}

void __ubsan_handle_divrem_overflow(struct OverflowData * data, unsigned long lhs, unsigned long rhs) {
	ubsan_debug(&data->location);
	fprintf(&_ubsan_log, "Overflow in divrem: %d %d\n", lhs, rhs);
}

void __ubsan_handle_negate_overflow(struct OverflowData * data, unsigned long old) {
	ubsan_debug(&data->location);
	fprintf(&_ubsan_log, "Overflow in negate: %d\n", old);
}

void __ubsan_handle_builtin_unreachable(struct UnreachableData * data) {
	ubsan_debug(&data->location);
	fprintf(&_ubsan_log, "called __builtin_unreachable()\n");
}

void __ubsan_handle_out_of_bounds(struct OutOfBoundsData * data, unsigned long index) {
	ubsan_debug(&data->location);
	fprintf(&_ubsan_log, "out of bounds array reference at %s[%d]\n", data->array_type->type_name, index);
}

void __ubsan_handle_shift_out_of_bounds(struct ShiftOutOfBoundsData * data, unsigned long lhs, unsigned long rhs) {
	ubsan_debug(&data->location);
	fprintf(&_ubsan_log, "shift is out of bounds: %d %d\n", lhs, rhs);
}

#define IS_ALIGNED(a, b) (((a) & ((__typeof__(a))(b)-1)) == 0)

void __ubsan_handle_type_mismatch(struct TypeMismatchData * data, unsigned long ptr) {
	return; /* Unaligned reads are valid on x86, and we have some very ugly code where this goes poorly. */
	ubsan_debug(&data->location);
	if (data->alignment && !IS_ALIGNED(ptr, data->alignment)) {
		fprintf(&_ubsan_log, "bad alignment in read at 0x%x (wanted %d)\n", ptr, data->alignment);
	} else {
		fprintf(&_ubsan_log, "type mismatch in reference at 0x%x\n", ptr);
	}
}

void __ubsan_handle_vla_bound_not_positive(struct VLABoundData * data, unsigned long bound) {
	ubsan_debug(&data->location);
	fprintf(&_ubsan_log, "vla bound not positive: %d\n", bound);
}

