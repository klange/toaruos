/*
 * Stateful UTF-8 decoder
 */
#include <stdint.h>

#define UTF8_ACCEPT 0
#define UTF8_REJECT 1

static inline uint32_t decode(uint32_t* state, uint32_t* codep, uint32_t byte) {
	static int state_table[32] = {
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0xxxxxxx */
		1,1,1,1,1,1,1,1,                 /* 10xxxxxx */
		2,2,2,2,                         /* 110xxxxx */
		3,3,                             /* 1110xxxx */
		4,                               /* 11110xxx */
		1                                /* 11111xxx */
	};

	static int mask_bytes[32] = {
		0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,
		0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x1F,0x1F,0x1F,0x1F,
		0x0F,0x0F,
		0x07,
		0x00
	};

	static int next[5] = {
		0,
		1,
		0,
		2,
		3
	};

	if (*state == UTF8_ACCEPT) {
		*codep = byte & mask_bytes[byte >> 3];
		*state = state_table[byte >> 3];
	} else if (*state > 0) {
		*codep = (byte & 0x3F) | (*codep << 6);
		*state = next[*state];
	}
	return *state;
}
