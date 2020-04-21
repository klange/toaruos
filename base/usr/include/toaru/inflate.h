/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2020 K. Lange
 */

#pragma once

#include <_cheader.h>
#include <stdint.h>

_Begin_C_Header

struct huff_ring;

struct inflate_context {

	/* Consumer-private pointers for input/output storage (eg. FILE *) */
	void * input_priv;
	void * output_priv;

	/* Methods for reading / writing from the input /output */
	uint8_t (*get_input)(struct inflate_context * ctx);
	void (*write_output)(struct inflate_context * ctx, unsigned int sym);

	/* Bit buffer, which holds at most 8 bits from the input */
	int bit_buffer;
	int buffer_size;

	/* Output ringbuffer for backwards lookups */
	struct huff_ring * ring;
};

int deflate_decompress(struct inflate_context * ctx);
int gzip_decompress(struct inflate_context * ctx);

_End_C_Header
