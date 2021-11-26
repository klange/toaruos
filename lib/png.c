/**
 * @brief PNG decoder.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2020 K. Lange
 */
#include <stdio.h>
#include <stdlib.h>

#include <toaru/graphics.h>
#include <toaru/inflate.h>

/**
 * Read 32-bit big-endian value from file.
 */
unsigned int read_32(FILE * f) {
	unsigned char a = fgetc(f);
	unsigned char b = fgetc(f);
	unsigned char c = fgetc(f);
	unsigned char d = fgetc(f);
	return (a << 24) | (b << 16) | (c << 8) | d;
}

/**
 * Read 16-bit big-endian value from file.
 */
unsigned int read_16(FILE * f) {
	unsigned char a = fgetc(f);
	unsigned char b = fgetc(f);
	return (a << 8) | b;
}

/**
 * (Debug) Return a chunk type as a string.
 */
__attribute__((unused))
static char*  reorder_type(unsigned int type) {
	static char out[4];
	out[0] = (type >> 24) & 0xFF;
	out[1] = (type >> 16) & 0xFF;
	out[2] = (type >> 8)  & 0xFF;
	out[3] = (type >> 0)  & 0xFF;
	return out;
}

/**
 * Internal PNG decoder state for use with inflate.
 */
struct png_ctx {
	FILE * f;          /* File being decoded. */
	sprite_t * sprite; /* Sprite being generated. */
	int y;             /* Cursor pointers for writing out bitmap data */
	int x;
	char buffer[4];   /* A buffer to hold a pixel's worth of data until it can
	                     be written out to the image with the right filter. */
	int  buf_off;     /* How much data is in the above buffer */
	int seen_ihdr;    /* Whether the IHDR was seen; for error handling */

	unsigned int width;   /* Image width (dup from sprite) */
	unsigned int height;  /* Image height (dup from sprite) */
	int bit_depth;        /* Bit depth of the image */
	int color_type;       /* PNG color type */
	int compression;      /* Compression method (must be 0) */
	int filter;           /* Filter method (must be 0) */
	int interlace;        /* Interlace method (we only support 0) */

	unsigned int size;    /* Remaining IDAT chunk size */
	int sf;               /* Current scanline filter type */
};

/* PNG chunk types */
#define PNG_IHDR 0x49484452
#define PNG_IDAT 0x49444154
#define PNG_IEND 0x49454e44

/* PNG filter types */
#define PNG_FILTER_NONE  0
#define PNG_FILTER_SUB   1
#define PNG_FILTER_UP    2
#define PNG_FILTER_AVG   3
#define PNG_FILTER_PAETH 4

/**
 * Read a byte from the IDAT chunk.
 * Tracks when an IDAT has been read to completion and
 * can load the next IDAT (or bail of this was the last one)
 */
static uint8_t _get(struct inflate_context * ctx) {
	struct png_ctx * c = (ctx->input_priv);
	if (c->size == 0) {

		/* Read the CRC32 from the end of this IDAT */
		unsigned int check = read_32(c->f);
		(void)check; /* ... and in theory check it... */

		/* Read the next IDAT chunk header */
		unsigned int size = read_32(c->f);
		unsigned int type = read_32(c->f);

		c->size = size;

		if (type != PNG_IDAT) {
			/* This isn't an IDAT? That's wrong! */
			fprintf(stderr, "And this is the wrong type (0x%x), I'm just bailing.\n", type);
			fprintf(stderr, "size read was 0x%x\n", size);
			exit(0);
		}
	}

	/* Read one byte from the input */
	c->size--;
	int i = fgetc(c->f);

	/* If this was EOF, we should handle that error case... probably... */
	if (i < 0) fprintf(stderr, "This is probably not good.\n");

	return i;
}

/**
 * Paeth predictor
 * Described in section 6.6 of the RFC
 */
#define ABS(a) ((a >= 0) ? (a) : -(a))
static int paeth(int a, int b, int c) {
	int p = a + b - c;
	int pa = ABS(p - a);
	int pb = ABS(p - b);
	int pc = ABS(p - c);
	if (pa <= pb && pa <= pc) return a;
	else if (pb <= pc) return b;
	return c;
}

static void write_pixel(struct png_ctx * c, uint32_t color) {
	SPRITE((c->sprite), (c->x), (c->y)) = color;

	/* Reset the short buffer */
	c->buf_off = 0;

	/* Advance to next pixel */
	c->x++;
	if (c->x == (int)c->width) {
		/* Advance to next line; next read is scanline filter type */
		c->x = -1;
		c->y++;
	}
}

static void process_pixel_type_6(struct png_ctx * c) {
	/*
	 * Obtain pixel data from short buffer;
	 * For color type 6, this is always in R G B A order in the
	 * bytestream, so we don't have to worry about subpixel ordering
	 * or weird color masks.
	 */
	unsigned int r = c->buffer[0];
	unsigned int g = c->buffer[1];
	unsigned int b = c->buffer[2];
	unsigned int a = c->buffer[3];

	/* Apply filters */
	if (c->sf == PNG_FILTER_SUB) {
		/* Add raw value to the pixel on the left */
		if (c->x > 0) {
			uint32_t left = SPRITE((c->sprite), (c->x - 1), (c->y));
			r += _RED(left);
			g += _GRE(left);
			b += _BLU(left);
			a += _ALP(left);
		}
	} else if (c->sf == PNG_FILTER_UP) {
		/* Add raw value to the pixel above */
		if (c->y > 0) {
			uint32_t up = SPRITE((c->sprite), (c->x), (c->y - 1));
			r += _RED(up);
			g += _GRE(up);
			b += _BLU(up);
			a += _ALP(up);
		}
	} else if (c->sf == PNG_FILTER_AVG) {
		/* Add raw value to the average of the pixel above and left */
		uint32_t left = (c->x > 0) ? SPRITE((c->sprite), (c->x - 1), (c->y)) : 0;
		uint32_t up = (c->y > 0) ? SPRITE((c->sprite), (c->x), (c->y - 1)) : 0;

		r += ((int)_RED(left) + (int)_RED(up)) / 2;
		g += ((int)_GRE(left) + (int)_GRE(up)) / 2;
		b += ((int)_BLU(left) + (int)_BLU(up)) / 2;
		a += ((int)_ALP(left) + (int)_ALP(up)) / 2;
	} else if (c->sf == PNG_FILTER_PAETH) {
		/* Use the Paeth predictor */
		uint32_t left = (c->x > 0) ? SPRITE((c->sprite), (c->x - 1), (c->y)) : 0;
		uint32_t up = (c->y > 0) ? SPRITE((c->sprite), (c->x), (c->y - 1)) : 0;
		uint32_t upleft = (c->x > 0 && c->y > 0) ? SPRITE((c->sprite), (c->x - 1), (c->y - 1)) : 0;

		r = ((int)r + paeth((int)_RED(left),(int)_RED(up),(int)_RED(upleft))) % 256;
		g = ((int)g + paeth((int)_GRE(left),(int)_GRE(up),(int)_GRE(upleft))) % 256;
		b = ((int)b + paeth((int)_BLU(left),(int)_BLU(up),(int)_BLU(upleft))) % 256;
		a = ((int)a + paeth((int)_ALP(left),(int)_ALP(up),(int)_ALP(upleft))) % 256;
	}

	/* Write new pixel to the image */
	write_pixel(c, rgba(r,g,b,a));
}

static void process_pixel_type_2(struct png_ctx * c) {
	/*
	 * Obtain pixel data from short buffer;
	 * For color type 6, this is always in R G B A order in the
	 * bytestream, so we don't have to worry about subpixel ordering
	 * or weird color masks.
	 */
	unsigned int r = c->buffer[0];
	unsigned int g = c->buffer[1];
	unsigned int b = c->buffer[2];

	/* Apply filters */
	if (c->sf == PNG_FILTER_SUB) {
		/* Add raw value to the pixel on the left */
		if (c->x > 0) {
			uint32_t left = SPRITE((c->sprite), (c->x - 1), (c->y));
			r += _RED(left);
			g += _GRE(left);
			b += _BLU(left);
		}
	} else if (c->sf == PNG_FILTER_UP) {
		/* Add raw value to the pixel above */
		if (c->y > 0) {
			uint32_t up = SPRITE((c->sprite), (c->x), (c->y - 1));
			r += _RED(up);
			g += _GRE(up);
			b += _BLU(up);
		}
	} else if (c->sf == PNG_FILTER_AVG) {
		/* Add raw value to the average of the pixel above and left */
		uint32_t left = (c->x > 0) ? SPRITE((c->sprite), (c->x - 1), (c->y)) : 0;
		uint32_t up = (c->y > 0) ? SPRITE((c->sprite), (c->x), (c->y - 1)) : 0;

		r += ((int)_RED(left) + (int)_RED(up)) / 2;
		g += ((int)_GRE(left) + (int)_GRE(up)) / 2;
		b += ((int)_BLU(left) + (int)_BLU(up)) / 2;
	} else if (c->sf == PNG_FILTER_PAETH) {
		/* Use the Paeth predictor */
		uint32_t left = (c->x > 0) ? SPRITE((c->sprite), (c->x - 1), (c->y)) : 0;
		uint32_t up = (c->y > 0) ? SPRITE((c->sprite), (c->x), (c->y - 1)) : 0;
		uint32_t upleft = (c->x > 0 && c->y > 0) ? SPRITE((c->sprite), (c->x - 1), (c->y - 1)) : 0;

		r = ((int)r + paeth((int)_RED(left),(int)_RED(up),(int)_RED(upleft))) % 256;
		g = ((int)g + paeth((int)_GRE(left),(int)_GRE(up),(int)_GRE(upleft))) % 256;
		b = ((int)b + paeth((int)_BLU(left),(int)_BLU(up),(int)_BLU(upleft))) % 256;
	}

	/* Write new pixel to the image */
	write_pixel(c, rgb(r,g,b));
}

static void process_pixel_type_4(struct png_ctx * c) {
	/*
	 * Obtain pixel data from short buffer;
	 * For color type 6, this is always in R G B A order in the
	 * bytestream, so we don't have to worry about subpixel ordering
	 * or weird color masks.
	 */
	unsigned int b = c->buffer[0];
	unsigned int a = c->buffer[1];

	/* Apply filters */
	if (c->sf == PNG_FILTER_SUB) {
		/* Add raw value to the pixel on the left */
		if (c->x > 0) {
			uint32_t left = SPRITE((c->sprite), (c->x - 1), (c->y));
			b += _BLU(left);
			a += _ALP(left);
		}
	} else if (c->sf == PNG_FILTER_UP) {
		/* Add raw value to the pixel above */
		if (c->y > 0) {
			uint32_t up = SPRITE((c->sprite), (c->x), (c->y - 1));
			b += _BLU(up);
			a += _ALP(up);
		}
	} else if (c->sf == PNG_FILTER_AVG) {
		/* Add raw value to the average of the pixel above and left */
		uint32_t left = (c->x > 0) ? SPRITE((c->sprite), (c->x - 1), (c->y)) : 0;
		uint32_t up = (c->y > 0) ? SPRITE((c->sprite), (c->x), (c->y - 1)) : 0;

		b += ((int)_BLU(left) + (int)_BLU(up)) / 2;
		a += ((int)_ALP(left) + (int)_ALP(up)) / 2;
	} else if (c->sf == PNG_FILTER_PAETH) {
		/* Use the Paeth predictor */
		uint32_t left = (c->x > 0) ? SPRITE((c->sprite), (c->x - 1), (c->y)) : 0;
		uint32_t up = (c->y > 0) ? SPRITE((c->sprite), (c->x), (c->y - 1)) : 0;
		uint32_t upleft = (c->x > 0 && c->y > 0) ? SPRITE((c->sprite), (c->x - 1), (c->y - 1)) : 0;

		b = ((int)b + paeth((int)_BLU(left),(int)_BLU(up),(int)_BLU(upleft))) % 256;
		a = ((int)a + paeth((int)_ALP(left),(int)_ALP(up),(int)_ALP(upleft))) % 256;
	}

	/* Write new pixel to the image */
	write_pixel(c, rgba(b,b,b,a));
}

static void process_pixel_type_0(struct png_ctx * c) {
	unsigned int b = c->buffer[0];
	if (c->sf == PNG_FILTER_SUB) {
		if (c->x > 0) {
			uint32_t left = SPRITE((c->sprite), (c->x - 1), (c->y));
			b += _BLU(left);
		}
	} else if (c->sf == PNG_FILTER_UP) {
		if (c->y > 0) {
			uint32_t up = SPRITE((c->sprite), (c->x), (c->y - 1));
			b += _BLU(up);
		}
	} else if (c->sf == PNG_FILTER_AVG) {
		uint32_t left = (c->x > 0) ? SPRITE((c->sprite), (c->x - 1), (c->y)) : 0;
		uint32_t up = (c->y > 0) ? SPRITE((c->sprite), (c->x), (c->y - 1)) : 0;
		b += ((int)_BLU(left) + (int)_BLU(up)) / 2;
	} else if (c->sf == PNG_FILTER_PAETH) {
		uint32_t left = (c->x > 0) ? SPRITE((c->sprite), (c->x - 1), (c->y)) : 0;
		uint32_t up = (c->y > 0) ? SPRITE((c->sprite), (c->x), (c->y - 1)) : 0;
		uint32_t upleft = (c->x > 0 && c->y > 0) ? SPRITE((c->sprite), (c->x - 1), (c->y - 1)) : 0;
		b = ((int)b + paeth((int)_BLU(left),(int)_BLU(up),(int)_BLU(upleft))) % 256;
	}

	/* Write new pixel to the image */
	write_pixel(c, rgb(b,b,b));
}


/**
 * Handle decompressed output from the inflater
 *
 * Writes pixel data to the image, and applies relevant filters.
 */
static void _write(struct inflate_context * ctx, unsigned int sym) {
	struct png_ctx * c = (ctx->input_priv);

	/* Put this byte into the short buffer */
	c->buffer[c->buf_off] = sym;
	c->buf_off++;

	/* If this is the beginning of a scanline... */
	if (c->x == -1 && c->buf_off == 1) {
		/* Then this is the scanline filter type */
		c->sf = sym;

		/* Reset the buffer, advance to the beginning of the actual scanline */
		c->x = 0;
		c->buf_off = 0;
	} else if (c->buf_off == 1 && c->color_type == 0) {
		process_pixel_type_0(c);
	} else if (c->buf_off == 2 && c->color_type == 4) {
		process_pixel_type_4(c);
	} else if (c->buf_off == 3 && c->color_type == 2) {
		process_pixel_type_2(c);
	} else if (c->buf_off == 4 && c->color_type == 6) {
		process_pixel_type_6(c);
	}
}

static int color_type_has_alpha(int c) {
	switch (c) {
		case 4:
		case 6:
			return ALPHA_EMBEDDED;
		default:
			return 0;
	}
}

int load_sprite_png(sprite_t * sprite, char * filename) {
	FILE * f = fopen(filename,"r");
	if (!f) {
		fprintf(stderr, "Failed to open file %s\n", filename);
		return 1;
	}

	/* Read the PNG signature */
	unsigned char sig[] = {137, 80, 78, 71, 13, 10, 26, 10};
	for (int i = 0; i < 8; ++i) {
		unsigned char c = fgetc(f);
		if (c != sig[i]) {
			fprintf(stderr, "byte %d (%d) does not match expected (%d)\n", i, c, sig[i]);
			goto _error;
		}
	}

	/* Set up context for future calls to inflate */
	struct png_ctx c;
	c.sprite = sprite;
	c.x = -1;
	c.y = 0;
	c.f = f;
	c.buf_off = 0;
	c.seen_ihdr = 0;

	while (1) {
		/* read chunks */
		unsigned int size = read_32(f);
		unsigned int type = read_32(f);

		if (feof(f)) break;

		switch (type) {
			case PNG_IHDR:
				{
					/* Image should only have one IHDR */
					if (c.seen_ihdr) return 1;

					c.seen_ihdr = 1;
					c.width = read_32(f); /* 4 */
					c.height = read_32(f); /* 8 */
					c.bit_depth = fgetc(f); /* 9 */
					c.color_type = fgetc(f); /* 10 */
					c.compression = fgetc(f); /* 11 */
					c.filter = fgetc(f); /* 12 */
					c.interlace = fgetc(f); /* 13 */

					/* Invalid / non-standard compression and filter types */
					if (c.compression != 0) return 1;
					if (c.filter != 0) return 1;

					/* 0 for none, 1 for Adam7 */
					if (c.interlace != 0 && c.interlace != 1) return 1;

					if (c.bit_depth != 8) return 1; /* Sorry */
					if (c.color_type < 0 || c.color_type > 6 || (c.color_type & 1)) return 1; /* Sorry, no indexed support */

					/* Allocate space */
					sprite->width  = c.width;
					sprite->height = c.height;
					sprite->bitmap = malloc(sizeof(uint32_t) * sprite->width * sprite->height);
					sprite->masks = NULL;
					sprite->alpha = color_type_has_alpha(c.color_type);
					sprite->blank = 0;


					/* Skip */
					for (unsigned int i = 13; i < size; ++i) fgetc(f);
				}
				break;

			case PNG_IDAT:
				{
					/* First two bytes of IDAT data are ZLIB header */
					unsigned int cflags = fgetc(f);
					if ((cflags & 0xF) != 8) {
						/* Compression type must be 8 */
						fprintf(stderr, "Expected flags to be 8 but it's 0x%x\n", cflags);
						return 1;
					}
					unsigned int aflags = fgetc(f);
					if (aflags & (1 << 5)) {
						fprintf(stderr, "There are preset bytes and I don't know what to do.\n");
						return 1;
					}

					struct inflate_context ctx;
					ctx.input_priv = &c;
					ctx.output_priv = &c;
					ctx.get_input = _get;
					ctx.write_output = _write;
					ctx.ring = NULL; /* use builtin */

					c.size = size - 2; /* 2 for the bytes we already read */

					deflate_decompress(&ctx);

					/* The IDATs contain a ZLIB stream, so they end with an
					 * adler32 checksum. Skip that. */
					unsigned int adler = read_32(f);
					(void)adler;
				}
				break;
			case PNG_IEND:
				/* We don't actually have anything to do here. */
				break;
			default:
				/* IHDR must be first */
				if (!c.seen_ihdr) return 1;
				//fprintf(stderr, "I don't know what this is! %4s 0x%x\n", reorder_type(type), type);
				/* Skip */
				for (unsigned int i = 0; i < size; ++i) fgetc(f);
				break;
		}

		unsigned int crc32 = read_32(f);
		(void)crc32;
	}

	/*
	 * Data in PNGs is unpremultiplied, but our sprites expect
	 * premultiplied alpha, so convert the image data
	 */
	for (int y = 0; y < sprite->height; ++y) {
		for (int x = 0; x < sprite->width; ++x) {
			SPRITE(sprite,x,y) = premultiply(SPRITE(sprite,x,y));
		}
	}

	return 0;

_error:
	fclose(f);
	return 1;
}
