/**
 * @brief libtoaru_jpeg: Decode simple JPEGs.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 *
 * Adapted from Raul Aguaviva's Python "micro JPEG visualizer":
 *
 * MIT License
 *
 * Copyright (c) 2017 Raul Aguaviva
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <toaru/graphics.h>

#if !defined(NO_SSE) && defined(__x86_64__)
#include <xmmintrin.h>
#include <emmintrin.h>
#endif

#if 0
#include <toaru/trace.h>
#define TRACE_APP_NAME "jpeg"
#else
#define TRACE(...)
#endif

static sprite_t * sprite = NULL;

/* Byte swap short (because JPEG uses big-endian values) */
static void swap16(uint16_t * val) {
	char * a = (char *)val;
	char b = a[0];
	a[0] = a[1];
	a[1] = b;
}

/* JPEG compontent zig-zag ordering */
static int zigzag[] = {
	 0,  1,  8, 16,  9,  2,  3, 10,
	17, 24, 32, 25, 18, 11,  4,  5,
	12, 19, 26, 33, 40, 48, 41, 34,
	27, 20, 13,  6,  7, 14, 21, 28,
	35, 42, 49, 56, 57, 50, 43, 36,
	29, 22, 15, 23, 30, 37, 44, 51,
	58, 59, 52, 45, 38, 31, 39, 46,
	53, 60, 61, 54, 47, 55, 62, 63
};

static uint8_t quant_mapping[3] = {0};
static uint8_t quant[8][64];

static int clamp(int col) {
	if (col > 255) return 255;
	if (col < 0) return 0;
	return col;
}

/* YCbCr to RGB conversion */
static void color_conversion(
		float Y, float Cb, float Cr,
		int *R, int *G, int *B
	) {
	float r = (Cr*(2.0-2.0*0.299) + Y);
	float b = (Cb*(2.0-2.0*0.114) + Y);
	float g = (Y - 0.144 * b - 0.229 * r) / 0.587;

	*R = clamp(r + 128);
	*G = clamp(g + 128);
	*B = clamp(b + 128);
}

static int xy_to_lin(int x, int y) {
	return x + y * 8;
}

struct huffman_table {
	uint8_t lengths[16];
	uint8_t elements[256];
} huffman_tables[256] = {0};

struct stream {
	FILE * file;
	uint8_t byte;
	int have;
	int pos;
};

static void define_quant_table(FILE * f, int len) {

	TRACE("Defining quant table");
	while (len > 0) {
		uint8_t hdr;
		fread(&hdr, 1, 1, f);
		fread(&quant[(hdr) & 0xF], 64, 1, f);
		len -= 65;
	}
	TRACE("Done");
}

static void baseline_dct(FILE * f, int len) {

	struct dct {
		uint8_t  hdr;
		uint16_t height;
		uint16_t width;
		uint8_t  components;
	} __attribute__((packed)) dct;

	fread(&dct, sizeof(struct dct), 1, f);

	/* Read image dimensions, each as big-endian 16-bit values */
	uint16_t h = dct.height;
	uint16_t w = dct.width;
	swap16(&h);
	swap16(&w);
	dct.height = h;
	dct.width = w;

	/* We read 7 bytes */
	len -= sizeof(struct dct);

	TRACE("Image dimensions are %d×%d", dct.width, dct.height);
	sprite->width  = dct.width;
	sprite->height = dct.height;
	sprite->bitmap = malloc(sizeof(uint32_t) * sprite->width * sprite->height);
	sprite->masks = NULL;
	sprite->alpha = 0;
	sprite->blank = 0;

	TRACE("Loading quantization mappings...");
	for (int i = 0; i < dct.components; ++i) {
		/* Quant mapping */
		struct {
			uint8_t id;
			uint8_t samp;
			uint8_t qtb_id;
		} __attribute__((packed)) tmp;

		fread(&tmp, sizeof(tmp), 1, f);

		/* There should only be three of these for the images we support. */
		if (i > 3) {
			abort();
		}

		quant_mapping[i] = tmp.qtb_id;

		/* 3 bytes were read */
		len -= 3;
	}

	/* Skip whatever else might be in this section */
	if (len > 0) {
		fseek(f, len, SEEK_CUR);
	}
}

static void define_huffman_table(FILE * f, int len) {

	TRACE("Loading Huffman tables...");
	while (len > 0) {
		/* Read header ID */
		uint8_t hdr;
		fread(&hdr, 1, 1, f);
		len--;

		/* Read length table */
		fread(huffman_tables[hdr].lengths, 16, 1, f);
		len -= 16;

		/* Read Huffman table entries */
		int o = 0;
		for (int i = 0; i < 16; ++i) {
			int l = huffman_tables[hdr].lengths[i];
			fread(&huffman_tables[hdr].elements[o], l, 1, f);
			o += l;
			len -= l;
		}
	}

	/* Skip rest of section */
	if (len > 0) {
		fseek(f, len, SEEK_CUR);
	}
}

struct idct {
	float base[64];
};

/**
 * norm_coeff[0] = 0.35355339059
 * norm_coeff[1] = 0.5
 */
static float cosines[8][8] = {
	{ 0.35355339059,0.35355339059,0.35355339059,0.35355339059,0.35355339059,0.35355339059,0.35355339059,0.35355339059 },
	{ 0.490392640202,0.415734806151,0.27778511651,0.0975451610081,-0.0975451610081,-0.27778511651,-0.415734806151,-0.490392640202 },
	{ 0.461939766256,0.191341716183,-0.191341716183,-0.461939766256,-0.461939766256,-0.191341716183,0.191341716183,0.461939766256 },
	{ 0.415734806151,-0.0975451610081,-0.490392640202,-0.27778511651,0.27778511651,0.490392640202,0.0975451610081,-0.415734806151 },
	{ 0.353553390593,-0.353553390593,-0.353553390593,0.353553390593,0.353553390593,-0.353553390593,-0.353553390593,0.353553390593 },
	{ 0.27778511651,-0.490392640202,0.0975451610081,0.415734806151,-0.415734806151,-0.0975451610081,0.490392640202,-0.27778511651 },
	{ 0.191341716183,-0.461939766256,0.461939766256,-0.191341716183,-0.191341716183,0.461939766256,-0.461939766256,0.191341716183 },
	{ 0.0975451610081,-0.27778511651,0.415734806151,-0.490392640202,0.490392640202,-0.415734806151,0.27778511651,-0.0975451610081 },
};

static float premul[8][8][8][8]= {{{{0}}}};

static void add_idc(struct idct * self, int n, int m, int coeff) {
#if defined(NO_SSE) || !defined(__x86_64__)
	for (int y = 0; y < 8; ++y) {
		for (int x = 0; x < 8; ++x) {
			self->base[xy_to_lin(x, y)] += premul[n][m][y][x] * coeff;
		}
	}
#else
	__m128 c = _mm_set_ps(coeff,coeff,coeff,coeff);
	for (int y = 0; y < 8; ++y) {
		__m128 a, b;
		/* base[y][x] = base[y][x] + premul[n][m][y][x] * coeff */

		/* x = 0..3 */
		a = _mm_load_ps(&premul[n][m][y][0]);
		a = _mm_mul_ps(a,c);
		b = _mm_load_ps(&self->base[xy_to_lin(0,y)]);
		a = _mm_add_ps(a,b);
		_mm_store_ps(&self->base[xy_to_lin(0,y)], a);

		/* x = 4..7 */
		a = _mm_load_ps(&premul[n][m][y][4]);
		a = _mm_mul_ps(a,c);
		b = _mm_load_ps(&self->base[xy_to_lin(4,y)]);
		a = _mm_add_ps(a,b);
		_mm_store_ps(&self->base[xy_to_lin(4,y)], a);
	}
#endif
}

static void add_zigzag(struct idct * self, int zi, int coeff) {
	int i = zigzag[zi];
	int n = i & 0x7;
	int m = i >> 3;
	add_idc(self, n, m, coeff);
}

/* Read a bit from the stream */
static int get_bit(struct stream * st) {
	while ((st->pos >> 3) >= st->have) {
		/* We have finished using the current byte and need to read another one */
		int t = fgetc(st->file);
		if (t < 0) {
			/* EOF */
			st->byte = 0;
		} else {
			st->byte = t;
		}

		if (st->byte == 0xFF) {
			/*
			 * If we see 0xFF, it's followed by a 0x00
			 * that should be skipped.
			 */
			int tmp = fgetc(st->file);
			if (tmp != 0) {
				/*
				 * If it's *not*, we reached the end of the file - but
				 * this shouldn't happen.
				 */
				st->byte = 0;
			}
		}

		/* We've seen a new byte */
		st->have++;
	}

	/* Extract appropriate bit from this byte */
	uint8_t b = st->byte;
	int s = 7 - (st->pos & 0x7);

	/* We move forward one position in the bit stream */
	st->pos += 1;
	return (b >> s) & 1;
}

/* Advance forward and get the n'th next bit */
static int get_bitn(struct stream * st, int l) {
	int val = 0;
	for (int i = 0; i < l; ++i) {
		val = val * 2 + get_bit(st);
	}
	return val;
}

/*
 * Read a Huffman code by reading bits and using
 * the Huffman table.
 */
static int get_code(struct huffman_table * table, struct stream * st) {
	int val = 0;
	int off = 0;
	int ini = 0;

	for (int i = 0; i < 16; ++i) {
		val = val * 2 + get_bit(st);
		if (table->lengths[i] > 0) {
			if (val - ini < table->lengths[i]) {
				return table->elements[off + val - ini];
			}
			ini = ini + table->lengths[i];
			off += table->lengths[i];
		}
		ini *= 2;
	}

	/* Invalid */
	return -1;
}

/* Decode Huffman codes to values */
static int decode(int code, int bits) {
	int l = 1L << (code - 1);
	if (bits >= l) {
		return bits;
	} else {
		return bits - (2 * l - 1);
	}
}

/* Build IDCT matrix */
static struct idct * build_matrix(struct idct * i, struct stream * st, int idx, uint8_t * quant, int oldcoeff, int * outcoeff) {
	memset(i, 0, sizeof(struct idct));

	int code = get_code(&huffman_tables[idx], st);
	int bits = get_bitn(st, code);
	int dccoeff = decode(code, bits) + oldcoeff;

	add_zigzag(i, 0, dccoeff * quant[0]);
	int l = 1;

	while (l < 64) {
		code = get_code(&huffman_tables[16+idx], st);
		if (code == 0) break;
		if (code > 15) {
			l += (code >> 4);
			code = code & 0xF;
		}
		bits = get_bitn(st, code);
		int coeff = decode(code, bits);
		add_zigzag(i, l, coeff * quant[l]);
		l += 1;
	}

	*outcoeff = dccoeff;
	return i;
}

/* Set pixel in sprite buffer with bounds checking */
static void set_pixel(int x, int y, uint32_t color) {
	if ((x < sprite->width) && (y < sprite->height)) {
		SPRITE(sprite,x,y) = color;
	}
}

/* Concvert YCbCr values to RGB pixels */
static void draw_matrix(int x, int y, struct idct * L, struct idct * cb, struct idct * cr) {
	for (int yy = 0; yy < 8; ++yy) {
		for (int xx = 0; xx < 8; ++xx) {
			int o = xy_to_lin(xx,yy);
			int r, g, b;
			color_conversion(L->base[o], cb->base[o], cr->base[o], &r, &g, &b);
			uint32_t c = 0xFF000000 | (r << 16) | (g << 8) | b;
			set_pixel((x * 8 + xx), (y * 8 + yy), c);
		}
	}
}

static void start_of_scan(FILE * f, int len) {

	TRACE("Reading image data");

	/* Skip header */
	fseek(f, len, SEEK_CUR);

	/* Initialize bit stream */
	struct stream _st = {0};
	_st.file = f;
	struct stream * st = &_st;

	int old_lum = 0;
	int old_crd = 0;
	int old_cbd = 0;
	for (int y = 0; y < sprite->height / 8 + !!(sprite->height & 0x7); ++y) {
		TRACE("Star row %d", y );
		for (int x = 0; x < sprite->width / 8 + !!(sprite->width & 0x7); ++x) {
			if (y >= 134) {
				TRACE("Start col %d", x);
			}

			/* Build matrices */
			struct idct matL, matCr, matCb;
			build_matrix(&matL,  st, 0, quant[quant_mapping[0]], old_lum, &old_lum);
			build_matrix(&matCb, st, 1, quant[quant_mapping[1]], old_cbd, &old_cbd);
			build_matrix(&matCr, st, 1, quant[quant_mapping[2]], old_crd, &old_crd);

			if (y >= 134) {
				TRACE("Draw col %d", x);
			}
			draw_matrix(x, y, &matL, &matCb, &matCr);
		}
	}

	TRACE("Done.");
}

int load_sprite_jpg(sprite_t * tsprite, char * filename) {
	FILE * f = fopen(filename, "r");
	if (!f) {
		return 1;
	}

	sprite = tsprite;

	memset(huffman_tables, 0, sizeof(huffman_tables));

	if (premul[0][0][0][0] == 0.0) {
		for (int n = 0; n < 8; ++n) {
			for (int m = 0; m < 8; ++m) {
				for (int y = 0; y < 8; ++y) {
					for (int x = 0; x < 8; ++x) {
						premul[n][m][y][x] = cosines[n][x] * cosines[m][y];
					}
				}
			}
		}
	}

	while (1) {

		/* Read a header */
		uint16_t hdr;
		int r = fread(&hdr, 2, 1, f);
		if (r <= 0) {
			/* EOF */
			break;
		}

		/* These headers are stored big-endian */
		swap16(&hdr);

		if (hdr == 0xffd8) {
			/* No data */
			continue;
		} else if (hdr == 0xffd9) {
			/* End of file */
			break;
		} else {
			/* Regular sections with data start with a length */
			uint16_t len;
			fread(&len, 2, 1, f);
			swap16(&len);

			/* Subtract two because the length includes itself */
			len -= 2;

			if (hdr == 0xffdb) {
				define_quant_table(f, len);
			} else if (hdr == 0xffc0) {
				baseline_dct(f, len);
			} else if (hdr == 0xffc4) {
				define_huffman_table(f, len);
			} else if (hdr == 0xffda) {
				start_of_scan(f, len);
				/* End immediately after reading the data */
				break;
			} else {
				TRACE("Unknown header\n");
				fseek(f, len, SEEK_CUR);
			}
		}
	}

	fclose(f);

	return 0;
}
