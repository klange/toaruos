/**
 * @brief Toaru Text library - TrueType parser.
 * @file lib/text.c
 * @author K. Lange <klange@toaruos.org>
 *
 * Implementation of TrueType font file parsing and basic
 * glyph rendering.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/shm.h>

#include <math.h>

#include <toaru/graphics.h>
#include <toaru/hashmap.h>
#include <toaru/decodeutf8.h>
#include <toaru/spinlock.h>

#include "toaru/text.h"

#undef min
#define min(a,b) ((a) < (b) ? (a) : (b))

struct TT_Table {
	off_t offset;
	size_t length;
};

struct TT_Coord {
	float x;
	float y;
};

struct TT_Line {
	struct TT_Coord start;
	struct TT_Coord end;
};

struct TT_Contour {
	size_t edgeCount;
	size_t nextAlloc;
	size_t flags;
	size_t last_start;
	struct TT_Line edges[];
};

struct TT_Intersection {
	float x;
	int affect;
};

struct TT_Edge {
	struct TT_Coord start;
	struct TT_Coord end;
	int direction;
};

struct TT_Shape {
	size_t edgeCount;
	int lastY;
	int startY;
	int lastX;
	int startX;
	struct TT_Edge edges[];
};

struct TT_Vertex {
	unsigned char flags;
	int x;
	int y;
};

struct TT_Font {
	int privFlags;
	FILE * filePtr;
	uint8_t * buffer;
	uint8_t * memPtr;

	struct TT_Table head_ptr;
	struct TT_Table cmap_ptr;
	struct TT_Table loca_ptr;
	struct TT_Table glyf_ptr;
	struct TT_Table hhea_ptr;
	struct TT_Table hmtx_ptr;
	struct TT_Table name_ptr;
	struct TT_Table os_2_ptr;

	off_t cmap_start;

	size_t cmap_maxInd;

	float scale;
	float emSize;

	int cmap_type;
	int loca_type;
};

/* Currently, the edge sorter is disabled. It doesn't really help much,
 * and it's very slow with our horrible qsort implementation. */
#if 0
static int edge_sorter_high_scanline(const void * a, const void * b) {
	const struct TT_Edge * left  = a;
	const struct TT_Edge * right = b;

	if (left->start.y < right->start.y) return -1;
	if (left->start.y > right->start.y) return 1;
	return 0;
}

static void sort_edges(size_t edgeCount, struct TT_Edge edges[edgeCount]) {
	qsort(edges, edgeCount, sizeof(struct TT_Edge), edge_sorter_high_scanline);
}
#endif

static int intersection_sorter(const void * a, const void * b) {
	const struct TT_Intersection * left  = a;
	const struct TT_Intersection * right = b;

	if (left->x < right->x) return -1;
	if (left->x > right->x) return 1;
	return 0;
}

static inline void sort_intersections(size_t cnt, struct TT_Intersection intersections[cnt]) {
	qsort(intersections, cnt, sizeof(struct TT_Intersection), intersection_sorter);
}

static inline float edge_at(float y, const struct TT_Edge * edge) {
	float u = (y - edge->start.y) / (edge->end.y - edge->start.y);
	return edge->start.x + u * (edge->end.x - edge->start.x);
}

__attribute__((hot))
static inline size_t prune_edges(size_t edgeCount, float y, const struct TT_Edge edges[edgeCount], struct TT_Intersection into[edgeCount]) {
	size_t outWriter = 0;
	for (size_t i = 0; i < edgeCount; ++i) {
		if (y > edges[i].end.y || y <= edges[i].start.y) continue;
		into[outWriter].x = edge_at(y,&edges[i]);
		into[outWriter].affect = edges[i].direction;
		outWriter++;
	}
	return outWriter;
}

static void process_scanline(
		float _y,
		const struct TT_Shape * shape,
		size_t subsample_width,
		float subsamples[subsample_width],
		size_t cnt,
		const struct TT_Intersection crosses[cnt]
	) {
	int wind = 0;
	size_t j = 0;
	for (int x = shape->startX; x < shape->lastX && j < cnt; ++x) {
		while (j < cnt && x > crosses[j].x) {
			wind += crosses[j].affect;
			j++;
		}
		float last = x;
		while (j < cnt && (x+1) > crosses[j].x) {
			if (wind != 0) {
				subsamples[x - shape->startX] += crosses[j].x - last;
			}
			last = crosses[j].x;
			wind += crosses[j].affect;
			j++;
		}
		if (wind != 0) {
			subsamples[x - shape->startX] += (x+1) - last;
		}
	}
}

static inline uint32_t tt_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
	return (a << 24U) | (r << 16) | (g << 8) | (b);
}

static inline uint32_t tt_apply_alpha(uint32_t color, uint16_t alpha) {
	uint8_t r = ((uint32_t)(_RED(color) * alpha + 0x80) * 0x101) >> 16UL;
	uint8_t g = ((uint32_t)(_GRE(color) * alpha + 0x80) * 0x101) >> 16UL;
	uint8_t b = ((uint32_t)(_BLU(color) * alpha + 0x80) * 0x101) >> 16UL;
	uint8_t a = ((uint32_t)(_ALP(color) * alpha + 0x80) * 0x101) >> 16UL;
	return tt_rgba(r,g,b,a);
}

static inline uint32_t tt_alpha_blend_rgba(uint32_t bottom, uint32_t top) {
	if (_ALP(bottom) == 0) return top;
	if (_ALP(top) == 255) return top;
	if (_ALP(top) == 0) return bottom;
	uint8_t a = _ALP(top);
	uint16_t t = 0xFF ^ a;
	uint8_t d_r = _RED(top) + (((uint32_t)(_RED(bottom) * t + 0x80) * 0x101) >> 16UL);
	uint8_t d_g = _GRE(top) + (((uint32_t)(_GRE(bottom) * t + 0x80) * 0x101) >> 16UL);
	uint8_t d_b = _BLU(top) + (((uint32_t)(_BLU(bottom) * t + 0x80) * 0x101) >> 16UL);
	uint8_t d_a = _ALP(top) + (((uint32_t)(_ALP(bottom) * t + 0x80) * 0x101) >> 16UL);
	return tt_rgba(d_r, d_g, d_b, d_a);
}


static void paint_scanline(gfx_context_t * ctx, int y, const struct TT_Shape * shape, float * subsamples, uint32_t color) {
	for (int x = shape->startX < 0 ? 0 : shape->startX; x < shape->lastX && x < ctx->width; ++x) {
		uint16_t na = (int)(255 * subsamples[x - shape->startX]) >> 2;
		uint32_t nc = tt_apply_alpha(color, na);
		GFX(ctx, x, y) = tt_alpha_blend_rgba(GFX(ctx, x, y), nc);
		subsamples[x-shape->startX] = 0;
	}
}

static inline int _is_in_clip(gfx_context_t * ctx, int32_t y) {
	if (!ctx->clips) return 1;
	if (y < 0 || y >= ctx->clips_size) return 1;
	return ctx->clips[y];
}

void tt_path_paint(gfx_context_t * ctx, const struct TT_Shape * shape, uint32_t color) {
	size_t size = shape->edgeCount;
	struct TT_Intersection * crosses = malloc(sizeof(struct TT_Intersection) * size);

	size_t subsample_width = shape->lastX - shape->startX;
	float * subsamples = malloc(sizeof(float) * subsample_width);
	memset(subsamples, 0, sizeof(float) * subsample_width);

	int startY = shape->startY < 0 ? 0 : shape->startY;
	int endY = shape->lastY <= ctx->height ? shape->lastY : ctx->height;

	for (int y = startY; y < endY; ++y) {
		if (!_is_in_clip(ctx,y)) continue;
		float _y = y + 0.0001;
		for (int l = 0; l < 4; ++l) {
			size_t cnt;
			if ((cnt = prune_edges(size, _y, shape->edges, crosses))) {
				sort_intersections(cnt, crosses);
				process_scanline(_y, shape, subsample_width, subsamples, cnt, crosses);
			}
			_y += 1.0/4.0;
		}
		paint_scanline(ctx, y, shape, subsamples, color);
	}

	free(subsamples);
	free(crosses);
}

struct TT_Contour * tt_contour_line_to(struct TT_Contour * shape, float x, float y) {
	if (shape->flags & 1) {
		shape->edges[shape->edgeCount].end.x = x;
		shape->edges[shape->edgeCount].end.y = y;
		shape->edgeCount++;
		shape->flags &= ~1;
	} else {
		if (shape->edgeCount + 1 == shape->nextAlloc) {
			shape->nextAlloc *= 2;
			shape = realloc(shape, sizeof(struct TT_Contour) + sizeof(struct TT_Line) * (shape->nextAlloc));
		}
		shape->edges[shape->edgeCount].start.x = shape->edges[shape->edgeCount-1].end.x;
		shape->edges[shape->edgeCount].start.y = shape->edges[shape->edgeCount-1].end.y;
		shape->edges[shape->edgeCount].end.x = x;
		shape->edges[shape->edgeCount].end.y = y;
		shape->edgeCount++;
		shape->flags &= ~1;
	}
	return shape;
}

struct TT_Contour * tt_contour_move_to(struct TT_Contour * shape, float x, float y) {
	if (!(shape->flags & 1) && shape->edgeCount) {
		shape = tt_contour_line_to(shape, shape->edges[shape->last_start].start.x, shape->edges[shape->last_start].start.y);
	}
	if (shape->edgeCount + 1 == shape->nextAlloc) {
		shape->nextAlloc *= 2;
		shape = realloc(shape, sizeof(struct TT_Contour) + sizeof(struct TT_Line) * (shape->nextAlloc));
	}
	shape->edges[shape->edgeCount].start.x = x;
	shape->edges[shape->edgeCount].start.y = y;
	shape->last_start = shape->edgeCount;
	shape->flags |= 1;
	return shape;
}

struct TT_Contour * tt_contour_start(float x, float y) {
	struct TT_Contour * shape = malloc(sizeof(struct TT_Contour) + sizeof(struct TT_Line) * 2);
	shape->edgeCount = 0;
	shape->nextAlloc = 2;
	shape->flags = 0;
	shape->last_start = 0;
	shape->edges[shape->edgeCount].start.x = x;
	shape->edges[shape->edgeCount].start.y = y;
	shape->last_start = shape->edgeCount;
	shape->flags |= 1;
	return shape;
}

struct TT_Shape * tt_contour_finish(const struct TT_Contour * in) {
	size_t size = in->edgeCount + 1;
	struct TT_Shape * tmp = malloc(sizeof(struct TT_Shape) + sizeof(struct TT_Edge) * size);
	for (size_t i = 0; i < in->edgeCount; ++i) {
		memcpy(&tmp->edges[i], &in->edges[i], sizeof(struct TT_Line));
	}

	if (in->flags & 1) {
		size--;
	} else {
		tmp->edges[in->edgeCount].start.x = in->edges[in->edgeCount-1].end.x;
		tmp->edges[in->edgeCount].start.y = in->edges[in->edgeCount-1].end.y;
		tmp->edges[in->edgeCount].end.x   = in->edges[in->last_start].start.x;
		tmp->edges[in->edgeCount].end.y   = in->edges[in->last_start].start.y;
	}

	for (size_t i = 0; i < size; ++i) {
		if (tmp->edges[i].start.y < tmp->edges[i].end.y) {
			tmp->edges[i].direction = 1;
		} else {
			tmp->edges[i].direction = -1;
			struct TT_Coord j = tmp->edges[i].start;
			tmp->edges[i].start = tmp->edges[i].end;
			tmp->edges[i].end = j;
		}
	}

	//sort_edges(size, tmp->edges);
	tmp->edgeCount = size;
	tmp->startY = INT_MAX;
	tmp->lastY = INT_MIN;
	tmp->startX = INT_MAX;
	tmp->lastX = INT_MIN;
	for (size_t i = 0; i < size; ++i) {
		if (tmp->edges[i].end.y + 1 > tmp->lastY) tmp->lastY = tmp->edges[i].end.y + 1;
		if (tmp->edges[i].start.y + 1 > tmp->lastY) tmp->lastY = tmp->edges[i].start.y + 1;
		if (tmp->edges[i].end.y < tmp->startY) tmp->startY = tmp->edges[i].end.y;
		if (tmp->edges[i].start.y < tmp->startY) tmp->startY = tmp->edges[i].start.y;

		if (tmp->edges[i].end.x + 2 > tmp->lastX) tmp->lastX = tmp->edges[i].end.x + 2;
		if (tmp->edges[i].start.x + 2 > tmp->lastX) tmp->lastX = tmp->edges[i].start.x + 2;
		if (tmp->edges[i].end.x < tmp->startX) tmp->startX = tmp->edges[i].end.x;
		if (tmp->edges[i].start.x < tmp->startX) tmp->startX = tmp->edges[i].start.x;
	}

	if (tmp->lastY < tmp->startY) tmp->startY = tmp->lastY;
	if (tmp->lastX < tmp->startX) tmp->startX = tmp->lastX;

	return tmp;
}

static inline int tt_seek(struct TT_Font * font, off_t offset) {
	if (font->privFlags & 1) {
		return fseek(font->filePtr, offset, SEEK_SET);
	} else {
		font->memPtr = font->buffer + offset;
		return 0;
	}
}

static inline long tt_tell(struct TT_Font * font) {
	if (font->privFlags & 1) {
		return ftell(font->filePtr);
	} else {
		return font->memPtr - font->buffer;
	}
}

static inline uint8_t tt_read_8(struct TT_Font * font) {
	if (font->privFlags & 1) {
		return fgetc(font->filePtr);
	} else {
		return *(font->memPtr++);
	}
}

static inline uint32_t tt_read_32(struct TT_Font * font) {
	int a = tt_read_8(font);
	int b = tt_read_8(font);
	int c = tt_read_8(font);
	int d = tt_read_8(font);
	if (a < 0 || b < 0 || c < 0 || d < 0) return 0;
	return ((a & 0xFF) << 24) |
	       ((b & 0xFF) << 16) |
	       ((c & 0xFF) << 8) |
	       ((d & 0xFF) << 0);
}

static inline uint16_t tt_read_16(struct TT_Font * font) {
	int a = tt_read_8(font);
	int b = tt_read_8(font);
	if (a < 0 || b < 0) return 0;
	return ((a & 0xFF) << 8) |
	       ((b & 0xFF) << 0);
}

int tt_measure_font(struct TT_Font * font, struct TT_FontMetrics * metrics) {
	int a, d, l;
	if (font->os_2_ptr.offset) {
		tt_seek(font, font->os_2_ptr.offset + 2 * 37);
		a = (int16_t)tt_read_16(font);
		d = -(int16_t)tt_read_16(font);

		tt_seek(font, font->hhea_ptr.offset + 2 * 4);
		l = (int16_t)tt_read_16(font);
	} else {
		tt_seek(font, font->hhea_ptr.offset + 2 * 2);
		a = (int16_t)tt_read_16(font);
		d = (int16_t)tt_read_16(font);
		l = (int16_t)tt_read_16(font);
	}

	metrics->ascender  = a * font->scale;
	metrics->descender = d * font->scale;
	metrics->lineGap   = l * font->scale;

	return 0;
}

int tt_xadvance_for_glyph(struct TT_Font * font, unsigned int ind) {
	tt_seek(font, font->hhea_ptr.offset + 2 * 17);
	uint16_t numLong = tt_read_16(font);

	if (ind < numLong) {
		tt_seek(font, font->hmtx_ptr.offset + ind * 4);
		return tt_read_16(font);
	}

	tt_seek(font, font->hmtx_ptr.offset + (numLong - 1) * 4);
	return tt_read_16(font);
}

void tt_set_size(struct TT_Font * font, float size) {
	font->scale = size / font->emSize;
}

void tt_set_size_px(struct TT_Font * font, float size) {
	tt_set_size(font, size * 4.0 / 3.0);
}

off_t tt_get_glyph_offset(struct TT_Font * font, unsigned int glyph) {
	if (font->loca_type == 0) {
		tt_seek(font, font->loca_ptr.offset + glyph * 2);
		return tt_read_16(font) * 2;
	} else {
		tt_seek(font, font->loca_ptr.offset + glyph * 4);
		return tt_read_32(font);
	}
}

int tt_glyph_for_codepoint(struct TT_Font * font, unsigned int codepoint) {
	if (font->cmap_type == 12) {
		/* Get group count */
		tt_seek(font, font->cmap_start + 4 + 8);
		uint32_t ngroups = tt_read_32(font);

		for (unsigned int i = 0; i < ngroups; ++i) {
			uint32_t start = tt_read_32(font);
			uint32_t end   = tt_read_32(font);
			uint32_t ind   = tt_read_32(font);

			if (codepoint >= start && codepoint <= end) {
				return ind + (codepoint - start);
			}
		}
	} else if (font->cmap_type == 4) {
		if (codepoint > 0xFFFF) return 0;

		tt_seek(font, font->cmap_start + 6);
		uint16_t segCount = tt_read_16(font) / 2;

		for (int i = 0; i < segCount; ++i) {
			tt_seek(font, font->cmap_start + 12 + 2 * i);
			uint16_t endCode = tt_read_16(font);
			if (endCode >= codepoint) {
				tt_seek(font, font->cmap_start + 12 + 2 * segCount + 2 + 2 * i);
				uint16_t startCode = tt_read_16(font);
				if (startCode > codepoint) {
					return 0;
				}
				tt_seek(font, font->cmap_start + 12 + 4 * segCount + 2 + 2 * i);
				int16_t idDelta = tt_read_16(font);
				tt_seek(font, font->cmap_start + 12 + 6 * segCount + 2 + 2 * i);
				uint16_t idRangeOffset = tt_read_16(font);
				if (idRangeOffset == 0) {
					return idDelta + codepoint;
				} else {
					tt_seek(font, font->cmap_start + 12 + 6 * segCount + 2 + 2 * i + idRangeOffset + (codepoint - startCode) * 2);
					return tt_read_16(font);
				}
			}
		}
	}

	return 0;
}

static void midpoint(float x_0, float y_0, float cx, float cy, float x_1, float y_1, float t, float * outx, float * outy) {
	float t2 = t * t;
	float nt = 1.0 - t;
	float nt2 = nt * nt;
	*outx = nt2 * x_0 + 2 * t * nt * cx + t2 * x_1;
	*outy = nt2 * y_0 + 2 * t * nt * cy + t2 * y_1;
}

__attribute__((visibility("protected")))
struct TT_Contour * tt_draw_glyph_into(struct TT_Contour * contour, struct TT_Font * font, float x_offset, float y_offset, unsigned int glyph) {
	off_t glyf_offset = tt_get_glyph_offset(font, glyph);
	if (tt_get_glyph_offset(font, glyph + 1) == glyf_offset) return contour;

	tt_seek(font, font->glyf_ptr.offset + glyf_offset);

	int16_t numContours = tt_read_16(font);
	/* int16_t xMin = */ tt_read_16(font);
	/* int16_t yMin = */ tt_read_16(font);
	/* int16_t xMax = */ tt_read_16(font);
	/* int16_t yMax = */ tt_read_16(font);

	tt_seek(font, font->glyf_ptr.offset + glyf_offset + 10);

	if (numContours > 0) {
		uint16_t endPt;
		for (int i = 0; i < numContours; ++i) {
			endPt = tt_read_16(font);
		}
		uint16_t numInstr = tt_read_16(font);
		for (unsigned int i = 0; i < numInstr; ++i) {
			tt_read_8(font);
		}
		struct TT_Vertex * vertices = malloc(sizeof(struct TT_Vertex) * (endPt + 1));
		for (int i = 0; i < endPt + 1; ) {
			uint8_t v = tt_read_8(font);
			vertices[i].flags = v;
			i++;
			if (v & 8) {
				uint8_t repC = tt_read_8(font);
				while (repC) {
					vertices[i].flags = v;
					repC--;
					i++;
				}
			}
		}
		int last_x = 0;
		int last_y = 0;
		for (int i = 0; i < endPt + 1; i++) {
			unsigned char flags = vertices[i].flags;
			if (flags & (1 << 1)) {
				/* One byte */
				if (flags & (1 << 4)) {
					/* Positive */
					vertices[i].x = last_x + tt_read_8(font);
				} else {
					vertices[i].x = last_x - tt_read_8(font);
				}
			} else {
				if (flags & (1 << 4)) {
					vertices[i].x = last_x;
				} else {
					int16_t diff = tt_read_16(font);
					vertices[i].x = last_x + diff;
				}
			}
			last_x = vertices[i].x;
		}
		for (int i = 0; i < endPt + 1; i++) {
			unsigned char flags = vertices[i].flags;
			if (flags & (1 << 2)) {
				/* One byte */
				if (flags & (1 << 5)) {
					/* Positive */
					vertices[i].y = last_y + tt_read_8(font);
				} else {
					vertices[i].y = last_y - tt_read_8(font);
				}
			} else {
				if (flags & (1 << 5)) {
					vertices[i].y = last_y;
				} else {
					int16_t diff = tt_read_16(font);
					vertices[i].y = last_y + diff;
				}
			}
			last_y = vertices[i].y;
		}

		tt_seek(font, font->glyf_ptr.offset + glyf_offset + 10);

		int move_next = 1;
		int next_end = tt_read_16(font);

		float lx = 0, ly = 0, cx = 0, cy = 0, x = 0, y = 0;
		float sx = 0, sy = 0;
		int wasControl = 0;

		for (int i = 0; i < endPt + 1; ++i) {
			x = ((float)vertices[i].x) * font->scale + x_offset;
			y = (-(float)vertices[i].y) * font->scale + y_offset;
			int isCurve = !(vertices[i].flags & (1 << 0));
			if (move_next) {
				contour = tt_contour_move_to(contour, x, y);
				if (isCurve) {
					/* Is the point before this on-curve? */
					float px = (float)vertices[next_end].x * font->scale + x_offset;
					float py = (-(float)vertices[next_end].y) * font->scale + y_offset;
					if (vertices[next_end].flags & (1 << 0)) {
						/* Else we're just a regular off-curve point? */
						sx = px;
						sy = py;
						lx = px;
						ly = py;
					} else {
						float dx = (px + x) / 2.0;
						float dy = (py + y) / 2.0;
						lx = dx;
						ly = dy;
						sx = dx;
						sy = dy;
					}
					cx = x;
					cy = y;
					wasControl = 1;
				} else {
					lx = x;
					ly = y;
					sx = x;
					sy = y;
					wasControl = 0;
				}
				move_next = 0;
			} else {
				if (isCurve) {
					if (wasControl) {
						float dx = (cx + x) / 2.0;
						float dy = (cy + y) / 2.0;
						for (int i = 1; i < 10; ++i) {
							float mx, my;
							midpoint(lx,ly,cx,cy,dx,dy,(float)i / 10.0,&mx,&my);
							contour = tt_contour_line_to(contour, mx, my);
						}
						contour = tt_contour_line_to(contour, dx, dy);
						lx = dx;
						ly = dy;
					}
					cx = x;
					cy = y;
					wasControl = 1;
				} else {
					if (wasControl) {
						for (int i = 1; i < 10; ++i) {
							float mx, my;
							midpoint(lx,ly,cx,cy,x,y,(float)i / 10.0,&mx,&my);
							contour = tt_contour_line_to(contour, mx, my);
						}
					}
					contour = tt_contour_line_to(contour, x, y);
					lx = x;
					ly = y;
					wasControl = 0;
				}
			}
			if (i == next_end) {
				if (wasControl) {
					for (int i = 1; i < 10; ++i) {
						float mx, my;
						midpoint(lx,ly,cx,cy,sx,sy,(float)i / 10.0,&mx,&my);
						contour = tt_contour_line_to(contour, mx, my);
					}
				}
				contour = tt_contour_line_to(contour, sx, sy);
				move_next = 1;
				next_end = tt_read_16(font);
			}
		}

		free(vertices);
	} else if (numContours < 0) {
		while (1) {
			uint16_t flags = tt_read_16(font);
			uint16_t ind   = tt_read_16(font);
			int16_t x, y;
			if (flags & (1 << 0)) {
				x = tt_read_16(font);
				y = tt_read_16(font);
			} else {
				x = tt_read_8(font);
				y = tt_read_8(font);
			}

			float x_f = x_offset;
			float y_f = y_offset;
			if (flags & (1 << 1)) {
				x_f = x_offset + x * font->scale;
				y_f = y_offset - y * font->scale;
			}

			if (flags & (1 << 3)) {
				/* TODO */
				tt_read_16(font);
			} else if (flags & (1 << 6)) {
				/* TODO */
				tt_read_16(font);
				tt_read_16(font);
			} else if (flags & (1 << 7)) {
				/* TODO */
				tt_read_16(font);
				tt_read_16(font);
				tt_read_16(font);
				tt_read_16(font);
			} else {
				long o = tt_tell(font);
				contour = tt_draw_glyph_into(contour,font,x_f,y_f,ind);
				tt_seek(font, o);
			}
			if (!(flags & (1 << 5))) break;
		}
	}

	return contour;
}

sprite_t * tt_bake_glyph(struct TT_Font * font, unsigned int glyph, uint32_t color, int *_x, int *_y, float xadjust) {
	struct TT_Contour * contour = tt_contour_start(0, 0);
	contour = tt_draw_glyph_into(contour,font,100+xadjust,100,glyph);
	if (!contour->edgeCount) {
		*_x = 0;
		*_y = 0;
		free(contour);
		return NULL;
	}

	/* Calculate bounds to render a sprite */
	struct TT_Shape * shape = tt_contour_finish(contour);
	int width = shape->lastX - shape->startX + 3;
	int height = shape->lastY - shape->startY + 2;

	int off_x = shape->startX - 1; shape->startX -= off_x; shape->lastX -= off_x;
	int off_y = shape->startY - 1; shape->startY -= off_y; shape->lastY -= off_y;

	/* Adjust the entire shape */
	for (size_t i = 0; i < shape->edgeCount; ++i) {
		shape->edges[i].start.x -= off_x;
		shape->edges[i].end.x -= off_x;
		shape->edges[i].start.y -= off_y;
		shape->edges[i].end.y -= off_y;
	}

	*_x = off_x - 100;
	*_y = off_y - 100;

	/* Create sprite */
	sprite_t * out = create_sprite(width,height,ALPHA_EMBEDDED);
	gfx_context_t * ctx = init_graphics_sprite(out);

	/* Fill to clear */
	draw_fill(ctx, 0);

	tt_path_paint(ctx, shape, color);

	free(ctx);
	free(shape);
	free(contour);
	return out;
}

void tt_draw_glyph(gfx_context_t * ctx, struct TT_Font * font, int x, int y, unsigned int glyph, uint32_t color) {
	struct TT_Contour * contour = tt_contour_start(0, 0);
	contour = tt_draw_glyph_into(contour,font,x,y,glyph);
	if (contour->edgeCount) {
		struct TT_Shape * shape = tt_contour_finish(contour);
		tt_path_paint(ctx, shape, color);
		free(shape);
	}
	free(contour);
}

int tt_string_width(struct TT_Font * font, const char * s) {
	float x_offset = 0;
	uint32_t cp = 0;
	uint32_t istate = 0;

	for (const unsigned char * c = (const unsigned char*)s; *c; ++c) {
		if (!decode(&istate, &cp, *c)) {
			unsigned int glyph = tt_glyph_for_codepoint(font, cp);
			x_offset += tt_xadvance_for_glyph(font, glyph) * font->scale;
		}
	}

	return x_offset;
}

int tt_string_width_int(struct TT_Font * font, const char * s) {
	int x_offset = 0;
	uint32_t cp = 0;
	uint32_t istate = 0;

	for (const unsigned char * c = (const unsigned char*)s; *c; ++c) {
		if (!decode(&istate, &cp, *c)) {
			unsigned int glyph = tt_glyph_for_codepoint(font, cp);
			x_offset += tt_xadvance_for_glyph(font, glyph) * font->scale;
		}
	}

	return x_offset;
}

float tt_glyph_width(struct TT_Font * font, unsigned int glyph) {
	return tt_xadvance_for_glyph(font, glyph) * font->scale;
}

__attribute__((visibility("protected")))
struct TT_Contour * tt_prepare_string_into(struct TT_Contour * contour, struct TT_Font * font, float x, float y, const char * s, float * out_width) {
	if (contour == NULL) {
		contour = tt_contour_start(0, 0);
	}

	float x_offset = x;
	uint32_t cp = 0;
	uint32_t istate = 0;

	for (const unsigned char * c = (const unsigned char*)s; *c; ++c) {
		if (!decode(&istate, &cp, *c)) {
			unsigned int glyph = tt_glyph_for_codepoint(font, cp);
			contour = tt_draw_glyph_into(contour,font,x_offset,y,glyph);
			x_offset += tt_xadvance_for_glyph(font, glyph) * font->scale;
		}
	}

	if (out_width) *out_width = x_offset - x;

	return contour;
}

__attribute__((visibility("protected")))
struct TT_Contour * tt_prepare_string(struct TT_Font * font, float x, float y, const char * s, float * out_width) {
	return tt_prepare_string_into(NULL, font, x, y, s, out_width);
}

int tt_draw_string(gfx_context_t * ctx, struct TT_Font * font, int x, int y, const char * s, uint32_t color) {
	float width;
	struct TT_Contour * contour = tt_prepare_string(font,x,y,s,&width);
	if (contour->edgeCount) {
		struct TT_Shape * shape = tt_contour_finish(contour);
		tt_path_paint(ctx, shape, color);
		free(shape);
	}
	free(contour);
	return width;
}


static int tt_font_load(struct TT_Font * font) {
	if (tt_seek(font, 4)) {
		fprintf(stderr, "tt: failed to seek to 4\n");
		goto _fail_free;
	}
	uint16_t numTables = tt_read_16(font);
	if (tt_seek(font, 12)) {
		fprintf(stderr, "tt: failed to seek to 12\n");
		goto _fail_free;
	}

	for (unsigned int i = 0; i < numTables; ++i) {
		uint32_t tag = tt_read_32(font);
		/* uint32_t checkSum = */ tt_read_32(font);
		uint32_t offset = tt_read_32(font);
		uint32_t length = tt_read_32(font);

		switch (tag) {
			case 0x68656164: /* head */
				font->head_ptr.offset = offset;
				font->head_ptr.length = length;
				break;
			case 0x636d6170: /* cmap */
				font->cmap_ptr.offset = offset;
				font->cmap_ptr.length = length;
				break;
			case 0x676c7966: /* glyf */
				font->glyf_ptr.offset = offset;
				font->glyf_ptr.length = length;
				break;
			case 0x6c6f6361: /* loca */
				font->loca_ptr.offset = offset;
				font->loca_ptr.length = length;
				break;
			case 0x68686561: /* hhea */
				font->hhea_ptr.offset = offset;
				font->hhea_ptr.length = length;
				break;
			case 0x686d7478: /* hmtx */
				font->hmtx_ptr.offset = offset;
				font->hmtx_ptr.length = length;
				break;
			case 0x6e616d65: /* name */
				font->name_ptr.offset = offset;
				font->name_ptr.length = length;
				break;
			case 0x4f532f32: /* OS/2 */
				font->os_2_ptr.offset = offset;
				font->name_ptr.length = length;
				break;
		}
	}

	if (!font->head_ptr.offset) { fprintf(stderr, "tt: no head table\n"); goto _fail_free; }
	if (!font->glyf_ptr.offset) { fprintf(stderr, "tt: no glyf table\n"); goto _fail_free; }
	if (!font->cmap_ptr.offset) { fprintf(stderr, "tt: no cmap table\n"); goto _fail_free; }
	if (!font->loca_ptr.offset) { fprintf(stderr, "tt: no loca table\n"); goto _fail_free; }

	/* Get emSize */
	tt_seek(font, font->head_ptr.offset + 18);
	font->emSize = (float)tt_read_16(font);

	/* Try to pick a viable cmap */
	tt_seek(font, font->cmap_ptr.offset);

	uint32_t best = 0;
	int bestScore = 0;

	/* Read size */
	/* uint16_t cmap_vers = */ tt_read_16(font);
	uint16_t cmap_size = tt_read_16(font);
	for (unsigned int i = 0; i < cmap_size; ++i) {
		uint16_t platform = tt_read_16(font);
		uint16_t type     = tt_read_16(font);
		uint32_t offset   = tt_read_32(font);

		if ((platform == 3 || platform == 0) && type == 10) {
			best = offset;
			bestScore = 4;
		} else if (platform == 0 && type == 4) {
			best = offset;
			bestScore = 4;
		} else if (((platform == 0 && type == 3) || (platform == 3 && type == 1)) && bestScore < 2) {
			best = offset;
			bestScore = 2;
		}
	}

	if (!best) {
		fprintf(stderr, "tt: TODO: unsupported cmap (best = %#x bestScore = %d)\n", best, bestScore);
		goto _fail_free;
	}

	/* What type is this */
	tt_seek(font, font->cmap_ptr.offset + best);

	font->cmap_type = tt_read_16(font);
	if (font->cmap_type != 12 && font->cmap_type != 4) {
		fprintf(stderr, "tt: TODO: unsupported cmap indexing %d\n", font->cmap_type);
		goto _fail_free;
	}

	font->cmap_start = font->cmap_ptr.offset + best;

	tt_seek(font, font->head_ptr.offset + 50);
	font->loca_type = tt_read_16(font);

	return 1;

_fail_free:
	return 0;
	free(font);
}

struct TT_Font * tt_font_from_file(const char * fileName) {
	FILE * f = fopen(fileName, "r");
	if (!f) return NULL;

	struct TT_Font * font = calloc(sizeof(struct TT_Font), 1);
	font->filePtr = f;
	font->privFlags = 1;

	if (!tt_font_load(font)) goto _fail_close;

	return font;

_fail_close:
	fclose(f);
	return NULL;
}

struct TT_Font * tt_font_from_memory(uint8_t * buffer) {
	struct TT_Font * font = calloc(sizeof(struct TT_Font), 1);
	font->privFlags = 0;
	font->buffer = buffer;
	if (!tt_font_load(font)) return NULL;
	return font;
}

struct TT_Font * tt_font_from_file_mem(const char * fileName) {
	FILE * f = fopen(fileName, "r");
	if (!f) return NULL;

	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);

	uint8_t * buf = malloc(size);
	fread(buf, 1, size, f);

	fclose(f);

	return tt_font_from_memory(buf);
}

static hashmap_t * shm_font_cache = NULL;
static int volatile shm_font_lock = 0;

struct TT_Font * tt_font_from_shm(const char * identifier) {
	spin_lock(&shm_font_lock);

	if (!shm_font_cache) {
		shm_font_cache = hashmap_create(10);
	}

	void * fontData = hashmap_get(shm_font_cache, (char*)identifier);
	if (fontData) goto shm_success;

	char * display = getenv("DISPLAY");

	if (!display) goto shm_fail;

	char fullIdentifier[1024];
	snprintf(fullIdentifier, 1023, "sys.%s.fonts.%s", display, identifier);

	size_t fontSize = 0;
	fontData = shm_obtain(fullIdentifier, &fontSize);

	if (fontSize == 0) {
		shm_release(identifier);
		goto shm_fail;
	}

	hashmap_set(shm_font_cache, (char*)identifier, fontData);

shm_success:
	spin_unlock(&shm_font_lock);
	return tt_font_from_memory(fontData);

shm_fail:
	spin_unlock(&shm_font_lock);
	return NULL;
}

void tt_draw_string_shadow(gfx_context_t * ctx, struct TT_Font * font, char * string, int font_size, int left, int top, uint32_t text_color, uint32_t shadow_color, int blur) {
	tt_set_size(font, font_size);
	int w = tt_string_width(font, string);
	/* TODO: We need to check the bounds of descenders and ascenders so we can fit things more correctly... */
	sprite_t * _tmp_s = create_sprite(w + blur * 2, font_size + blur * 2 + 5, ALPHA_EMBEDDED);
	gfx_context_t * _tmp = init_graphics_sprite(_tmp_s);
	draw_fill(_tmp, rgba(0,0,0,0));
	tt_draw_string(_tmp, font, blur, blur + font_size, string, shadow_color);
	blur_context_box(_tmp, blur);
	blur_context_box(_tmp, blur);
	free(_tmp);
	draw_sprite(ctx, _tmp_s, left - blur, top - blur);
	sprite_free(_tmp_s);
	tt_draw_string(ctx, font, left, top + font_size, string, text_color);
}

static int to_eight(uint32_t codepoint, char * out) {
	memset(out, 0x00, 7);

	if (codepoint < 0x0080) {
		out[0] = (char)codepoint;
	} else if (codepoint < 0x0800) {
		out[0] = 0xC0 | (codepoint >> 6);
		out[1] = 0x80 | (codepoint & 0x3F);
	} else if (codepoint < 0x10000) {
		out[0] = 0xE0 | (codepoint >> 12);
		out[1] = 0x80 | ((codepoint >> 6) & 0x3F);
		out[2] = 0x80 | (codepoint & 0x3F);
	} else if (codepoint < 0x200000) {
		out[0] = 0xF0 | (codepoint >> 18);
		out[1] = 0x80 | ((codepoint >> 12) & 0x3F);
		out[2] = 0x80 | ((codepoint >> 6) & 0x3F);
		out[3] = 0x80 | ((codepoint) & 0x3F);
	} else if (codepoint < 0x4000000) {
		out[0] = 0xF8 | (codepoint >> 24);
		out[1] = 0x80 | (codepoint >> 18);
		out[2] = 0x80 | ((codepoint >> 12) & 0x3F);
		out[3] = 0x80 | ((codepoint >> 6) & 0x3F);
		out[4] = 0x80 | ((codepoint) & 0x3F);
	} else {
		out[0] = 0xF8 | (codepoint >> 30);
		out[1] = 0x80 | ((codepoint >> 24) & 0x3F);
		out[2] = 0x80 | ((codepoint >> 18) & 0x3F);
		out[3] = 0x80 | ((codepoint >> 12) & 0x3F);
		out[4] = 0x80 | ((codepoint >> 6) & 0x3F);
		out[5] = 0x80 | ((codepoint) & 0x3F);
	}

	return strlen(out);
}


char * tt_get_name_string(struct TT_Font * font, int identifier) {
	if (!font->name_ptr.offset) return NULL;

	tt_seek(font, font->name_ptr.offset);
	uint16_t nameFormat = tt_read_16(font);
	uint16_t count = tt_read_16(font);
	uint16_t stringOffset = tt_read_16(font);

	if (nameFormat != 0) return NULL; /* Unsupported table format */

	/* Read records until we find one that matches what we asked for, in a suitable format */
	for (unsigned int i = 0; i < count; ++i) {
		uint16_t platformId = tt_read_16(font);
		uint16_t platformSpecificId = tt_read_16(font);
		/* uint16_t languageId = */ tt_read_16(font);
		uint16_t nameId = tt_read_16(font);
		uint16_t length = tt_read_16(font);
		uint16_t offset = tt_read_16(font);

		if (nameId != identifier) continue;
		if (!(platformId == 3 && platformSpecificId == 1)) continue;

		char * tmp = calloc(length * 3 + 1,1); /* Should be enough ? */
		char * c = tmp;

		tt_seek(font, stringOffset + offset + font->name_ptr.offset);

		for (unsigned int j = 0; j < length; j += 2) {
			uint32_t cp = tt_read_16(font);
			if (cp > 0xD7FF && cp < 0xE000) {
				uint32_t highBits = cp - 0xD800;
				uint32_t lowBits = tt_read_16(font) - 0xDC00;
				cp = 0x10000 + (highBits << 10) + lowBits;
				j += 2;
			}
			c += to_eight(cp, c);
		}

		return tmp;
	}

	return NULL;
}

struct PenPoly {
	float x;
	float y;
	float inner;
	float outer;
	float basis;
};

static float tangent(float x_0, float y_0, float x_1, float y_1) {
	return fmod(atan2(y_0 - y_1, x_1 - x_0) + 2.0 * M_PI, 2.0 * M_PI);
}

static int angle_compare(float s, struct PenPoly * pen, int a) {
	if (s >= pen[a].inner && s < pen[a].outer) return 0;
	if (s >= pen[a].inner && pen[a].outer < pen[a].inner) return 0;
	if (s <  pen[a].outer && pen[a].outer < pen[a].inner) return 0;
	if (s <  pen[a].outer && (2.0 * M_PI + s - pen[a].outer > M_PI)) return 1;
	if (s >  pen[a].outer && (s - pen[a].outer > M_PI)) return 1;
	return -1;
}

static int best_angle(int sides, struct PenPoly * pen, float s) {
	for (int a = 0; a < sides; ++a) {
		if (angle_compare(s,pen,a) == 0) return a;
	}
	return 0;
}

__attribute__((visibility("protected")))
struct TT_Contour * tt_contour_stroke_contour(const struct TT_Contour * in, float width) {
	struct TT_Contour * stroke = tt_contour_start(0,0);

	if (in->edgeCount) {
		int sides = width < 1.0 ? 4 : 16;
		float inner = 2.0 * M_PI / (float)sides;
		float outer = (M_PI - inner) / 2.0;
		struct PenPoly * pen = malloc(sizeof(struct PenPoly) * sides); /* Arbitrary */
		for (int i = 0; i < sides; ++i) {
			float angle = (float)i * 2.0 * M_PI / (float)sides;
			pen[i].x = cos(angle) * width;
			pen[i].y = -sin(angle) * width;
			pen[i].basis = angle;
			pen[i].inner = fmod(angle + outer, 2.0 * M_PI);
			pen[i].outer = fmod(angle + outer + inner, 2.0 * M_PI);
		}


		int start_of_segment = 0;
		int next_segment = (int)in->edgeCount;

		do {
			int started = 0;
			int v = start_of_segment;
			float s = tangent(in->edges[v].start.x, in->edges[v].start.y, in->edges[v].end.x, in->edges[v].end.y);
			int a = best_angle(sides,pen,s);

			while (v < (int)in->edgeCount) {
				s = tangent(in->edges[v].start.x, in->edges[v].start.y, in->edges[v].end.x, in->edges[v].end.y);
				stroke = (started ? tt_contour_line_to : tt_contour_move_to)(stroke, pen[a].x + in->edges[v].start.x, pen[a].y + in->edges[v].start.y);
				started = 1;
				int comp = angle_compare(s,pen,a);
				if (comp == 0) {
					if (v + 1 == (int)in->edgeCount) {
						next_segment = in->edgeCount;
						break;
					}
					if (in->edges[v+1].start.x != in->edges[v].end.x || in->edges[v+1].start.y != in->edges[v].end.y) {
						next_segment = v + 1;
						break;
					}
					v++;
				} else if (comp == 1) {
					a = (sides + a - 1) % sides;
				} else {
					a = (a + 1) % sides;
				}
			}
			while (v >= start_of_segment) {
				s = tangent(in->edges[v].end.x, in->edges[v].end.y, in->edges[v].start.x, in->edges[v].start.y);
				stroke = tt_contour_line_to(stroke, in->edges[v].end.x + pen[a].x, in->edges[v].end.y + pen[a].y);
				int comp = angle_compare(s,pen,a);
				if (comp == 0) {
					if (v == start_of_segment) break;
					v--;
				} else if (comp == 1) {
					a = (sides + a - 1) % sides;
				} else {
					a = (a + 1) % sides;
				}
			}
			while (v == start_of_segment) {
				s = tangent(in->edges[v].start.x, in->edges[v].start.y, in->edges[v].end.x, in->edges[v].end.y);
				stroke = tt_contour_line_to(stroke, pen[a].x + in->edges[v].start.x, pen[a].y + in->edges[v].start.y);
				int comp = angle_compare(s,pen,a);
				if (comp == 0) {
					break;
				} else if (comp == 1) {
					a = (sides + a - 1) % sides;
				} else {
					a = (a + 1) % sides;
				}
			}
			start_of_segment = next_segment;
		} while (next_segment != (int)in->edgeCount);

		free(pen);
	}

	return stroke;
}

struct TT_Shape * tt_contour_stroke_shape(const struct TT_Contour * in, float width) {
	struct TT_Contour * stroke = tt_contour_stroke_contour(in,width);
	struct TT_Shape * out = tt_contour_finish(stroke);
	free(stroke);
	return out;
}

void tt_contour_transform(struct TT_Contour * cnt, gfx_matrix_t matrix) {
	for (size_t i = 0; i < cnt->edgeCount; i++) {
		double x, y;
		gfx_apply_matrix(cnt->edges[i].start.x, cnt->edges[i].start.y, matrix, &x, &y);
		cnt->edges[i].start.x = x;
		cnt->edges[i].start.y = y;
		gfx_apply_matrix(cnt->edges[i].end.x, cnt->edges[i].end.y, matrix, &x, &y);
		cnt->edges[i].end.x = x;
		cnt->edges[i].end.y = y;
	}
}

static inline int out_of_bounds(const sprite_t * tex, int x, int y) {
	return x < 0 || y < 0 || x >= tex->width || y >= tex->height;
}

static inline uint32_t linear_interp(uint32_t left, uint32_t right, uint16_t pr) {
	uint16_t pl = 0xFF ^ pr;
	uint8_t d_r = (((uint32_t)(_RED(right) * pr + 0x80) * 0x101) >> 16UL) + (((uint32_t)(_RED(left) * pl + 0x80) * 0x101) >> 16UL);
	uint8_t d_g = (((uint32_t)(_GRE(right) * pr + 0x80) * 0x101) >> 16UL) + (((uint32_t)(_GRE(left) * pl + 0x80) * 0x101) >> 16UL);
	uint8_t d_b = (((uint32_t)(_BLU(right) * pr + 0x80) * 0x101) >> 16UL) + (((uint32_t)(_BLU(left) * pl + 0x80) * 0x101) >> 16UL);
	uint8_t d_a = (((uint32_t)(_ALP(right) * pr + 0x80) * 0x101) >> 16UL) + (((uint32_t)(_ALP(left) * pl + 0x80) * 0x101) >> 16UL);
	return tt_rgba(d_r, d_g, d_b, d_a);
}

static inline uint32_t sprite_pixel_no_repeat(const sprite_t * tex, int x, int y) {
	return out_of_bounds(tex,x,y) ? 0 : SPRITE(tex,x,y);
}

static inline int wrap(int x, int w) {
	return x < 0 ? (w - 1 - (-x -1) % w) : (x % w);
}

static inline uint32_t sprite_pixel_repeat(const sprite_t * tex, int x, int y) {
	int w = tex->width;
	int h = tex->height;
	return SPRITE(tex,wrap(x,w),wrap(y,h));
}

static inline uint32_t sprite_pixel_pad(const sprite_t * tex, int x, int y) {
	int w = tex->width;
	int h = tex->height;
	if (x < 0) x = 0;
	if (x >= w) x = w-1;
	if (y < 0) y = 0;
	if (y >= h) y = h-1;
	return SPRITE(tex,x,y);
}

typedef uint32_t (*pixel_getter_t)(const sprite_t*,int,int);

__attribute__((hot))
static inline uint32_t sprite_interpolate_bilinear(const sprite_t * tex, double u, double v, pixel_getter_t pixel_getter) {
	int x = floor(u);
	int y = floor(v);
	uint32_t ul = pixel_getter(tex,x,y);
	uint32_t ur = pixel_getter(tex,x+1,y);
	uint32_t ll = pixel_getter(tex,x,y+1);
	uint32_t lr = pixel_getter(tex,x+1,y+1);
	if ((ul | ur | ll | lr) == 0) return 0;
	uint8_t u_ratio = (u - x) * 0xFF;
	uint8_t v_ratio = (v - y) * 0xFF;
	uint32_t top = linear_interp(ul,ur,u_ratio);
	uint32_t bot = linear_interp(ll,lr,u_ratio);
	return linear_interp(top,bot,v_ratio);
}

static inline uint32_t sprite_interpolate_nearest(const sprite_t * tex, double u, double v, pixel_getter_t pixel_getter) {
	int x = floor(u);
	int y = floor(v);
	return pixel_getter(tex,x,y);
}

typedef uint32_t (*sprite_interp_t)(const sprite_t *, double, double, pixel_getter_t);

__attribute__((hot))
static inline void paint_scanline_sprite(gfx_context_t * ctx, int y, const struct TT_Shape * shape, float * subsamples, sprite_t * sprite, double u, double v, double filter_dxx, double filter_dxy, sprite_interp_t sprite_interp, pixel_getter_t pixel_getter) {
	for (int x = shape->startX < 0 ? 0 : shape->startX; x < shape->lastX && x < ctx->width; ++x) {
		uint16_t na = (int)(255 * subsamples[x - shape->startX]) >> 2;
		uint32_t color = sprite_interp(sprite, u, v, pixel_getter);
		uint32_t nc = tt_apply_alpha(color, na);
		GFX(ctx, x, y) = tt_alpha_blend_rgba(GFX(ctx, x, y), nc);
		subsamples[x-shape->startX] = 0;
		u += filter_dxx;
		v += filter_dxy;
	}
}

static inline void tt_path_paint_sprite_internal(gfx_context_t * ctx, const struct TT_Shape * shape, sprite_t * sprite, gfx_matrix_t matrix, sprite_interp_t sprite_interp, pixel_getter_t pixel_getter) {
	gfx_matrix_t inverse;
	gfx_matrix_invert(matrix,inverse);
	size_t size = shape->edgeCount;
	struct TT_Intersection * crosses = malloc(sizeof(struct TT_Intersection) * size);

	size_t subsample_width = shape->lastX - shape->startX;
	float * subsamples = malloc(sizeof(float) * subsample_width);
	memset(subsamples, 0, sizeof(float) * subsample_width);

	int startY = shape->startY < 0 ? 0 : shape->startY;
	int endY = shape->lastY <= ctx->height ? shape->lastY : ctx->height;

	double filter_x, filter_y, filter_dxx, filter_dxy, filter_dyx, filter_dyy;
	int _left = shape->startX < 0 ? 0 : shape->startX;
	gfx_apply_matrix(_left, startY, inverse, &filter_x, &filter_y);
	gfx_apply_matrix(_left+1, startY, inverse, &filter_dxx, &filter_dxy);
	filter_dxx -= filter_x;
	filter_dxy -= filter_y;
	gfx_apply_matrix(_left, startY+1, inverse, &filter_dyx, &filter_dyy);
	filter_dyx -= filter_x;
	filter_dyy -= filter_y;

	for (int y = startY; y < endY; ++y) {
		float u = filter_x;
		float v = filter_y;
		filter_x += filter_dyx;
		filter_y += filter_dyy;
		if (!_is_in_clip(ctx,y)) continue;
		float _y = y + 0.0001;
		for (int l = 0; l < 4; ++l) {
			size_t cnt;
			if ((cnt = prune_edges(size, _y, shape->edges, crosses))) {
				sort_intersections(cnt, crosses);
				process_scanline(_y, shape, subsample_width, subsamples, cnt, crosses);
			}
			_y += 1.0/4.0;
		}
		paint_scanline_sprite(ctx, y, shape, subsamples, sprite, u, v, filter_dxx, filter_dxy, sprite_interp, pixel_getter);
	}

	free(subsamples);
	free(crosses);
}

void tt_path_paint_sprite(gfx_context_t * ctx, const struct TT_Shape * shape, sprite_t * sprite, gfx_matrix_t matrix) {
	tt_path_paint_sprite_internal(ctx,shape,sprite,matrix,sprite_interpolate_bilinear,sprite_pixel_repeat);
}

void tt_path_paint_sprite_options(gfx_context_t * ctx, const struct TT_Shape * shape, sprite_t * sprite, gfx_matrix_t matrix, int filter, int wrap) {
	sprite_interp_t sprite_interp = sprite_interpolate_bilinear;
	pixel_getter_t pixel_getter = sprite_pixel_repeat;

	switch (filter) {
		case TT_PATH_FILTER_BILINEAR:
		default:
			break;
		case TT_PATH_FILTER_NEAREST:
			sprite_interp = sprite_interpolate_nearest;
			break;
	}

	switch (wrap) {
		case TT_PATH_WRAP_REPEAT:
		default:
			break;
		case TT_PATH_WRAP_NONE:
			pixel_getter = sprite_pixel_no_repeat;
			break;
		case TT_PATH_WRAP_PAD:
			pixel_getter = sprite_pixel_pad;
			break;
	}

	tt_path_paint_sprite_internal(ctx,shape,sprite,matrix,sprite_interp,pixel_getter);
}

char * tt_ellipsify(const char * input, int font_size, struct TT_Font * font, int max_width, int * out_width) {
	int width;
	int len = strlen(input);
	char * out = malloc(len + 4);

	if (max_width <= 0) {
		out[0] = '\0';
		width = 0;
		goto _finish;
	}

	memcpy(out, input, len + 1);
	tt_set_size(font, font_size);
	while ((width = tt_string_width(font, out)) > max_width) {
		len--;
		if (len+0>=0) out[len+0] = '.';
		if (len+1>=0) out[len+1] = '.';
		if (len+2>=0) out[len+2] = '.';
		out[len+3] = '\0';
	}

_finish:
	if (out_width) *out_width = width;
	return out;
}
