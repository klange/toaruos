#pragma once

#include <types.h>

struct TypeDescriptor  {
	uint16_t type_kind;
	uint16_t type_info;
	char type_name[1];
};

struct SourceLocation {
	const char *file_name;
	uint32_t line;
	uint32_t column;
};

struct OverflowData {
	struct SourceLocation location;
	struct TypeDescriptor *type;
};

struct TypeMismatchData {
	struct SourceLocation location;
	struct TypeDescriptor *type;
	unsigned long alignment;
	unsigned char type_check_kind;
};

struct NonnullArgData {
	struct SourceLocation location;
	struct SourceLocation attr_location;
	int arg_index;
};

struct NonnullReturnData {
	struct SourceLocation location;
	struct SourceLocation attr_location;
};

struct VLABoundData {
	struct SourceLocation location;
	struct TypeDescriptor *type;
};

struct OutOfBoundsData {
	struct SourceLocation location;
	struct TypeDescriptor *array_type;
	struct TypeDescriptor *index_type;
};

struct ShiftOutOfBoundsData {
	struct SourceLocation location;
	struct TypeDescriptor *lhs_type;
	struct TypeDescriptor *rhs_type;
};

struct UnreachableData {
	struct SourceLocation location;
};

struct InvalidValueData {
	struct SourceLocation location;
	struct TypeDescriptor *type;
};

typedef int64_t s_max;
typedef uint64_t u_max;

