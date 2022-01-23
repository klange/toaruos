#pragma once
#include <stdint.h>

struct regs {
	uint64_t x30, user_sp;
	uint64_t x28, x29;
	uint64_t x26, x27;
	uint64_t x24, x25;
	uint64_t x22, x23;
	uint64_t x20, x21;
	uint64_t x18, x19;
	uint64_t x16, x17;
	uint64_t x14, x15;
	uint64_t x12, x13;
	uint64_t x10, x11;
	uint64_t x8, x9;
	uint64_t x6, x7;
	uint64_t x4, x5;
	uint64_t x2, x3;
	uint64_t x0, x1;
};

