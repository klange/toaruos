/**
 * @brief Float printer.
 *
 * This is the float printer from Kuroko, minus the Kuroko parts.
 *
 * Not very efficient.
 *
 * Comments have been stripped for brevity.
 *
 * @ref https://github.com/kuroko-lang/kuroko/blob/master/src/obj_long.c
 *
 * @author K. Lange <klange@toaruos.org>
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "libc/internal.h"
#include "libc/stdio/stdio_internal.h"

struct StringBuilder {
	size_t capacity;
	size_t length;
	char * bytes;
};
static void krk_pushStringBuilder(struct StringBuilder *sb, char c);
static void krk_pushStringBuilderStr(struct StringBuilder *sb, const char *s, size_t len);
static char * krk_finishStringBuilder(struct StringBuilder *sb);

#define DIGIT_SHIFT 31
#define DIGIT_MAX   0x7FFFFFFF

struct KrkLong_Internal {
	ssize_t    width;
	uint32_t *digits;
};

typedef struct KrkLong_Internal KrkLong;
typedef KrkLong krk_long[1];

static int krk_long_init_si(KrkLong * num, int64_t val) {
	if (val == 0) {
		num->width = 0;
		num->digits = NULL;
		return 0;
	}

	int sign = (val < 0) ? -1 : 1;
	uint64_t abs = (val < 0) ? -val : val;

	if (abs <= DIGIT_MAX) {
		num->width = sign;
		num->digits = malloc(sizeof(uint32_t));
		num->digits[0] = abs;
		return 0;
	}

	uint64_t tmp = abs;
	int64_t cnt = 1;

	while (tmp > DIGIT_MAX) {
		cnt++;
		tmp >>= DIGIT_SHIFT;
	}

	num->width = cnt * sign;
	num->digits = malloc(sizeof(uint32_t) * cnt);

	for (int64_t i = 0; i < cnt; ++i) {
		num->digits[i] = (abs & DIGIT_MAX);
		abs >>= DIGIT_SHIFT;
	}

	return 0;
}

static int krk_long_init_many(KrkLong *a, ...) {
	va_list argp;
	va_start(argp, a);

	KrkLong * next = a;
	while (next) {
		krk_long_init_si(next, 0);
		next = va_arg(argp, KrkLong *);
	}

	va_end(argp);
	return 0;
}

static int krk_long_init_copy(KrkLong * out, const KrkLong * in) {
	size_t abs_width = in->width < 0 ? -in->width : in->width;
	out->width = in->width;
	out->digits = out->width ? malloc(sizeof(uint32_t) * abs_width) : NULL;
	for (size_t i = 0; i < abs_width; ++i) {
		out->digits[i] = in->digits[i];
	}
	return 0;
}

static int krk_long_clear(KrkLong * num) {
	if (num->digits) free(num->digits);
	num->width = 0;
	num->digits = NULL;
	return 0;
}

static int krk_long_clear_many(KrkLong *a, ...) {
	va_list argp;
	va_start(argp, a);

	KrkLong * next = a;
	while (next) {
		krk_long_clear(next);
		next = va_arg(argp, KrkLong *);
	}

	va_end(argp);
	return 0;
}

static int _swap(KrkLong * a, KrkLong * b) {
	ssize_t width = a->width;
	uint32_t * digits = a->digits;
	a->width = b->width;
	a->digits = b->digits;
	b->width = width;
	b->digits = digits;
	return 0;
}

static int krk_long_resize(KrkLong * num, ssize_t newdigits) {
	if (newdigits == 0) {
		krk_long_clear(num);
		return 0;
	}

	size_t abs = newdigits < 0 ? -newdigits : newdigits;
	size_t eabs = num->width < 0 ? -num->width : num->width;
	if (num->width == 0) {
		num->digits = calloc(newdigits, sizeof(uint32_t));
	} else if (eabs < abs) {
		num->digits = realloc(num->digits, sizeof(uint32_t) * newdigits);
		memset(&num->digits[eabs], 0, sizeof(uint32_t)*(abs-eabs));
	}

	num->width = newdigits;
	return 0;
}

static int krk_long_set_sign(KrkLong * num, int sign) {
	num->width = num->width < 0 ? (-num->width) * sign : num->width * sign;
	return 0;
}

static int krk_long_trim(KrkLong * num) {
	int invert = num->width < 0;
	size_t owidth = invert ? -num->width : num->width;
	size_t redundant = 0;
	for (size_t i = 0; i < owidth; i++) {
		if (num->digits[owidth-i-1] == 0) {
			redundant++;
		} else {
			break;
		}
	}

	if (redundant) {
		krk_long_resize(num, owidth - redundant);
		if (invert) krk_long_set_sign(num, -1);
	}

	return 0;
}

static size_t _bits_in(const KrkLong * num) {
	if (num->width == 0) return 0;

	size_t abs_width = num->width < 0 ? -num->width : num->width;

	size_t c = 0;
	uint32_t digit = num->digits[abs_width-1];
	while (digit) {
		c++;
		digit >>= 1;
	}

	return c + (abs_width-1) * DIGIT_SHIFT;
}

static size_t _bit_is_set(const KrkLong * num, size_t bit) {
	size_t digit_offset = bit / DIGIT_SHIFT;
	size_t digit_bit    = bit % DIGIT_SHIFT;
	return !!(num->digits[digit_offset] & (1 << digit_bit));
}

static int krk_long_sign(const KrkLong * num) {
	if (num->width == 0) return 0;
	return num->width < 0 ? -1 : 1;
}

static int krk_long_add_ignore_sign(KrkLong * res, const KrkLong * a, const KrkLong * b) {
	size_t awidth = a->width < 0 ? -a->width : a->width;
	size_t bwidth = b->width < 0 ? -b->width : b->width;
	size_t owidth = awidth < bwidth ? bwidth + 1 : awidth + 1;
	size_t carry  = 0;
	krk_long_resize(res, owidth);
	for (size_t i = 0; i < owidth - 1; ++i) {
		uint32_t out = (i < awidth ? a->digits[i] : 0) + (i < bwidth ? b->digits[i] : 0) + carry;
		res->digits[i] = out & DIGIT_MAX;
		carry = out > DIGIT_MAX;
	}
	if (carry) {
		res->digits[owidth-1] = 1;
	} else {
		krk_long_resize(res, owidth - 1);
	}
	return 0;
}

static int _sub_big_small(KrkLong * res, const KrkLong * a, const KrkLong * b) {
	size_t awidth = a->width < 0 ? -a->width : a->width;
	size_t bwidth = b->width < 0 ? -b->width : b->width;
	size_t owidth = awidth;

	krk_long_resize(res, owidth);

	int carry = 0;

	for (size_t i = 0; i < owidth; ++i) {
		int64_t a_digit = (int64_t)(i < awidth ? a->digits[i] : 0) - carry;
		int64_t b_digit = i < bwidth ? b->digits[i] : 0;
		if (a_digit < b_digit) {
			a_digit += (int64_t)1 << DIGIT_SHIFT;
			carry = 1;
		} else {
			carry = 0;
		}

		res->digits[i] = (a_digit - b_digit) & DIGIT_MAX;
	}

	krk_long_trim(res);

	return 0;
}

static int krk_long_compare_abs(const KrkLong * a, const KrkLong * b) {
	size_t a_width = a->width < 0 ? -a->width : a->width;
	size_t b_width = b->width < 0 ? -b->width : b->width;
	if (a_width > b_width) return 1;
	if (b_width > a_width) return -1;
	size_t abs_width = a_width;
	for (size_t i = 0; i < abs_width; ++i) {
		if (a->digits[abs_width-i-1] > b->digits[abs_width-i-1]) return 1;
		if (a->digits[abs_width-i-1] < b->digits[abs_width-i-1]) return -1;
	}
	return 0;
}

#define PREP_OUTPUT(res,a,b) KrkLong _tmp_out_ ## res, *_swap_out_ ## res = NULL; do { if (res == a || res == b) { krk_long_init_si(&_tmp_out_ ## res, 0); _swap_out_ ## res = res; res = &_tmp_out_ ## res; } } while (0)
#define PREP_OUTPUT1(res,a) KrkLong _tmp_out_ ## res, *_swap_out_ ## res = NULL; do { if (res == a) { krk_long_init_si(&_tmp_out_ ## res, 0); _swap_out_ ## res = res;  res = &_tmp_out_ ## res; } } while (0)
#define FINISH_OUTPUT(res) do { if (_swap_out_ ## res) { _swap(_swap_out_ ## res, res); krk_long_clear(&_tmp_out_ ## res); } } while (0)

static int krk_long_add(KrkLong * res, const KrkLong * a, const KrkLong * b) {
	PREP_OUTPUT(res,a,b);

	if (a->width == 0) {
		krk_long_clear(res);
		krk_long_init_copy(res,b);
		FINISH_OUTPUT(res);
		return 0;
	} else if (b->width == 0) {
		krk_long_clear(res);
		krk_long_init_copy(res,a);
		FINISH_OUTPUT(res);
		return 0;
	}

	if (a->width < 0 && b->width > 0) {
		switch (krk_long_compare_abs(a,b)) {
			case -1:
				_sub_big_small(res,b,a);
				krk_long_set_sign(res,1);
				FINISH_OUTPUT(res);
				return 0;
			case 1:
				_sub_big_small(res,a,b);
				krk_long_set_sign(res,-1);
				FINISH_OUTPUT(res);
				return 0;
		}
		krk_long_clear(res);
		FINISH_OUTPUT(res);
		return 0;
	} else if (a->width > 0 && b->width < 0) {
		switch (krk_long_compare_abs(a,b)) {
			case -1:
				_sub_big_small(res,b,a);
				krk_long_set_sign(res,-1);
				FINISH_OUTPUT(res);
				return 0;
			case 1:
				_sub_big_small(res,a,b);
				krk_long_set_sign(res,1);
				FINISH_OUTPUT(res);
				return 0;
		}
		krk_long_clear(res);
		FINISH_OUTPUT(res);
		return 0;
	}

	int sign = a->width < 0 ? -1 : 1;
	if (krk_long_add_ignore_sign(res,a,b)) {
		FINISH_OUTPUT(res);
		return 1;
	}
	krk_long_set_sign(res,sign);
	FINISH_OUTPUT(res);
	return 0;
}

static int krk_long_sub(KrkLong * res, const KrkLong * a, const KrkLong * b) {
	PREP_OUTPUT(res,a,b);
	if (a->width == 0) {
		krk_long_clear(res);
		krk_long_init_copy(res,b);
		krk_long_set_sign(res, b->width < 0 ? 1 : -1);
		FINISH_OUTPUT(res);
		return 0;
	} else if (b->width == 0) {
		krk_long_clear(res);
		krk_long_init_copy(res,a);
		FINISH_OUTPUT(res);
		return 0;
	}

	if ((a->width < 0) != (b->width < 0)) {
		if (krk_long_add_ignore_sign(res,a,b)) { FINISH_OUTPUT(res); return 1; }
		krk_long_set_sign(res,a->width < 0 ? -1 : 1);
		FINISH_OUTPUT(res);
		return 0;
	}

	switch (krk_long_compare_abs(a,b)) {
		case 0:
			krk_long_clear(res);
			FINISH_OUTPUT(res);
			return 0;
		case 1:
			_sub_big_small(res,a,b);
			if (a->width < 0) krk_long_set_sign(res, -1);
			FINISH_OUTPUT(res);
			return 0;
		case -1:
			_sub_big_small(res,b,a);
			if (b->width > 0) krk_long_set_sign(res, -1);
			FINISH_OUTPUT(res);
			return 0;
	}

	__builtin_unreachable();
}

static int krk_long_zero(KrkLong * num) {
	size_t abs_width = num->width < 0 ? -num->width : num->width;
	for (size_t i = 0; i < abs_width; ++i) {
		num->digits[i] = 0;
	}
	return 0;
}

static int krk_long_bit_set(KrkLong * num, size_t bit) {
	size_t abs_width = num->width < 0 ? -num->width : num->width;
	size_t digit_offset = bit / DIGIT_SHIFT;
	size_t digit_bit    = bit % DIGIT_SHIFT;

	if (digit_offset >= abs_width) {
		krk_long_resize(num, digit_offset+1);
		for (size_t i = abs_width; i < digit_offset + 1; ++i) {
			num->digits[i] = 0;
		}
	}

	num->digits[digit_offset] |= (1 << digit_bit);
	return 0;
}

static int _mul_abs(KrkLong * res, const KrkLong * a, const KrkLong * b) {

	size_t awidth = a->width < 0 ? -a->width : a->width;
	size_t bwidth = b->width < 0 ? -b->width : b->width;

	krk_long_resize(res, awidth+bwidth);
	krk_long_zero(res);

	for (size_t i = 0; i < bwidth; ++i) {
		uint64_t b_digit = b->digits[i];
		uint64_t carry = 0;
		for (size_t j = 0; j < awidth; ++j) {
			uint64_t a_digit = a->digits[j];
			uint64_t tmp = carry + a_digit * b_digit + res->digits[i+j];
			carry = tmp >> DIGIT_SHIFT;
			res->digits[i+j] = tmp & DIGIT_MAX;
		}
		res->digits[i + awidth] = carry;
	}

	krk_long_trim(res);

	return 0;
}

static int krk_long_mul(KrkLong * res, const KrkLong * a, const KrkLong * b) {
	PREP_OUTPUT(res,a,b);

	if (a->width == 0) {
		krk_long_clear(res);
		krk_long_init_copy(res,a);
		FINISH_OUTPUT(res);
		return 0;
	}

	if (b->width == 0) {
		krk_long_clear(res);
		krk_long_init_copy(res,b);
		FINISH_OUTPUT(res);
		return 0;
	}

	if (_mul_abs(res,a,b)) {
		FINISH_OUTPUT(res);
		return 1;
	}

	if ((a->width < 0) == (b->width < 0)) {
		krk_long_set_sign(res,1);
	} else {
		krk_long_set_sign(res,-1);
	}

	FINISH_OUTPUT(res);
	return 0;
}

static void _krk_long_lshift_z(krk_long out, krk_long val, size_t amount) {
	if (amount == 0) {
		krk_long_clear(out);
		krk_long_init_copy(out,val);
		return;
	}

	int64_t count = _bits_in(val);
	krk_long_clear(out);
	if (count == 0) return;

	size_t offset = amount % 31;
	size_t cycles = amount / 31;
	ssize_t w = val->width < 0 ? -val->width : val->width;
	krk_long_bit_set(out, count - 1 + amount);

	if (!offset) {
		for (ssize_t i = 0; i < w; ++i) {
			out->digits[i+cycles] = val->digits[i];
		}
	} else {
		uint32_t shift_in = 0;
		for (ssize_t i = 0; i < w; ++i) {
			out->digits[i+cycles] = ((val->digits[i] << offset) & DIGIT_MAX) | shift_in;
			shift_in = (val->digits[i] >> (31 - offset)) & DIGIT_MAX;
		}
		if (shift_in) {
			out->digits[w+cycles] = shift_in;
		}
	}

	if (krk_long_sign(val) < 0) krk_long_set_sign(out,-1);
}

static void _krk_long_rshift_z(krk_long out, krk_long val, size_t amount) {
	if (amount == 0) {
		krk_long_clear(out);
		krk_long_init_copy(out,val);
		return;
	}

	int64_t count = _bits_in(val);
	krk_long_clear(out);
	if (count == 0) return;

	if (amount < (size_t)count) {
		size_t offset = amount % 31;
		size_t cycles = amount / 31;
		ssize_t w = val->width < 0 ? -val->width : val->width;
		krk_long_bit_set(out, count - 1 - amount);

		if (!offset) {
			for (ssize_t i = cycles; i < w; ++i) {
				out->digits[i-cycles] = val->digits[i];
			}
		} else {
			out->digits[0] = (val->digits[cycles] >> offset) & DIGIT_MAX;
			for (size_t i = 1; i < (size_t)out->width; ++i) {
				out->digits[i-1] |= (val->digits[i+cycles] << (31 - offset)) & DIGIT_MAX;
				out->digits[i] = (val->digits[i+cycles] >> offset) & DIGIT_MAX;
			}
			if (out->width+cycles < (size_t)w) {
				out->digits[out->width-1] |= (val->digits[out->width+cycles] << (31 - offset)) & DIGIT_MAX;
			}
		}
	}

	if (krk_long_sign(val) < 0) {
		KrkLong one;
		krk_long_init_si(&one, 1);
		krk_long_add(out,out,&one);
		krk_long_set_sign(out,-1);
		krk_long_clear(&one);
	}
}

typedef uint32_t digit_t;
#define DEC_DIGIT_SIZE sizeof(digit_t)
#define DEC_DIGIT_CNT  9
#define DEC_DIGIT_MAX 1000000000

static digit_t * dec_add(const digit_t * a, size_t awidth, const digit_t * b, size_t bwidth, size_t * outwidth) {
	*outwidth = (awidth > bwidth ? awidth : bwidth) + 1;
	digit_t * out = calloc(*outwidth, DEC_DIGIT_SIZE);
	int64_t carry = 0;
	for (size_t i  = 0; i < *outwidth - 1; ++i) {
		digit_t n = ((i < awidth) ? a[i] : 0) + ((i < bwidth) ? b[i] : 0) + carry;
		out[i] = n % DEC_DIGIT_MAX;
		carry = (n >= DEC_DIGIT_MAX);
	}
	if (carry) {
		out[*outwidth-1] = 1;
	} else {
		*outwidth -= 1;
	}

	if (*outwidth == 0) {
		*outwidth = 1;
		out[0] = 0;
	}

	return out;
}

static void dec_isub(digit_t * a, size_t awidth, const digit_t * b, size_t bwidth) {
	int64_t carry = 0;
	for (size_t i = 0; i < awidth; ++i) {
		int64_t a_digit = (int64_t)((i < awidth) ? a[i] : 0) - carry;
		int64_t b_digit = (int64_t)((i < bwidth) ? b[i] : 0);
		if (a_digit < b_digit) {
			a_digit += DEC_DIGIT_MAX;
			carry = 1;
		} else {
			carry = 0;
		}
		a[i] = (a_digit - b_digit) % DEC_DIGIT_MAX;
	}
}

static digit_t * dec_shift(const digit_t * a, size_t awidth, size_t amount, size_t * outwidth) {
	if (awidth == 1 && a[0] == 0) {
		*outwidth = 1;
		return calloc(1,DEC_DIGIT_SIZE);
	}
	*outwidth = awidth + amount;
	digit_t * out = calloc(*outwidth,DEC_DIGIT_SIZE);

	for (size_t i = 0; i < awidth; ++i) {
		out[i+amount] = a[i];
	}

	return out;
}

static digit_t * dec_mul(const digit_t * a, size_t a_width, const digit_t * b, size_t b_width, size_t * outwidth) {
	if (a_width < b_width) {
		const digit_t * t = a;
		a = b;
		b = t;
		size_t tmp = a_width;
		a_width = b_width;
		b_width = tmp;
	}

	*outwidth = a_width + b_width;

	if ((a_width == 1 && a[0] == 0) || (b_width == 1 && b[0] == 0)) {
		*outwidth = 1;
		return calloc(1,DEC_DIGIT_SIZE);
	}

	if (a_width == 1 && a[0] == 1) {
		*outwidth = b_width;
		digit_t * out = malloc(*outwidth * DEC_DIGIT_SIZE);
		memcpy(out, b, *outwidth * DEC_DIGIT_SIZE);
		return out;
	}

	if (b_width == 1 && b[0] == 1) {
		*outwidth = a_width;
		digit_t * out = malloc(*outwidth * DEC_DIGIT_SIZE);
		memcpy(out, a, *outwidth * DEC_DIGIT_SIZE);
		return out;
	}

	if (b_width < 50) {
		digit_t * out = calloc(*outwidth,DEC_DIGIT_SIZE);
		for (size_t i = 0; i < b_width; ++i) {
			digit_t bdigit = (i < b_width) ? b[i] : 0;
			int64_t carry = 0;
			for (size_t j = 0; j < a_width; ++j) {
				digit_t adigit = (j < a_width) ? a[j] : 0;
				uint64_t t = carry + (int64_t)adigit * (int64_t)bdigit + out[i+j];
				carry = t / DEC_DIGIT_MAX;
				out[i+j] = t % DEC_DIGIT_MAX;
			}
			out[i+a_width] = carry;
		}
		while (*outwidth > 1 && out[(*outwidth)-1] == 0) (*outwidth)--;
		return out;
	} else {
		size_t m2  = a_width / 2;

		const digit_t * low1  = a;
		size_t    low1_width = (m2 <= a_width) ? m2 : a_width;
		while (low1_width > 1 && low1[low1_width-1] == 0) low1_width--;
		digit_t   a_zero = 0;
		const digit_t * high1 = (m2 <= a_width) ? (a + m2) : &a_zero;
		size_t    high1_width = (m2 <= a_width) ? (a_width - m2) : 1;

		const digit_t * low2  = b;
		size_t    low2_width = (m2 <= b_width) ? m2 : b_width;
		while (low2_width > 1 && low2[low2_width-1] == 0) low2_width--;
		digit_t   b_zero = 0;
		const digit_t * high2 = (m2 <= b_width) ? (b + m2) : &b_zero;
		size_t    high2_width = (m2 <= b_width) ? (b_width - m2) : 1;

		size_t z0_width, z1_width, z2_width;

		digit_t * z0 = dec_mul(low1, low1_width, low2, low2_width, &z0_width);
		digit_t * z2 = dec_mul(high1, high1_width, high2, high2_width, &z2_width);

		size_t sleft_width, sright_width;
		digit_t * sleft  = dec_add(low1, low1_width, high1, high1_width, &sleft_width);
		digit_t * sright = dec_add(low2, low2_width, high2, high2_width, &sright_width);
		digit_t * z1 = dec_mul(sleft, sleft_width, sright, sright_width, &z1_width);
		free(sleft);
		free(sright);

		dec_isub(z1, z1_width, z2, z2_width);
		dec_isub(z1, z1_width, z0, z0_width);

		size_t m2_shift_width;
		digit_t * m2_shift = dec_shift(z1, z1_width, m2, &m2_shift_width);
		free(z1);

		size_t add_width;
		digit_t * add = dec_add(m2_shift, m2_shift_width, z0, z0_width, &add_width);
		free(m2_shift);
		free(z0);

		size_t m2_2_width;
		digit_t * m2_2 = dec_shift(z2, z2_width, m2 * 2, &m2_2_width);
		free(z2);

		size_t result_width;
		digit_t * result = dec_add(m2_2, m2_2_width, add, add_width, &result_width);
		free(m2_2);
		free(add);

		*outwidth = result_width;
		return result;
	}
}

static digit_t * dec_two_raised(size_t w, size_t * sizeOut) {
	if (w <= 29) {
		*sizeOut = 1;
		digit_t * out = malloc(DEC_DIGIT_SIZE);
		out[0] = 1 << w;
		return out;
	} else {
		size_t w2 = w >> 1;
		size_t tSize;
		digit_t * t = dec_two_raised(w2, &tSize);
		if ((w & 1) == 0) {
			digit_t * result = dec_mul(t, tSize, t, tSize, sizeOut);
			free(t);
			return result;
		} else {
			size_t wmw2 = w - w2;
			size_t rightSize;
			digit_t * right = dec_two_raised(wmw2, &rightSize);
			digit_t * result = dec_mul(t, tSize, right, rightSize, sizeOut);
			free(t);
			free(right);
			return result;
		}
	}
}

static digit_t * long_to_dec_inner(KrkLong * n, size_t w, size_t * sizeOut) {
	if (n->width == 0) {
		*sizeOut = 1;
		return calloc(1,DEC_DIGIT_SIZE);
	}
	if (w <= 29) {
		*sizeOut = 1;
		digit_t * out = malloc(DEC_DIGIT_SIZE);
		out[0] = n->digits[0];
		return out;
	}

	size_t aSize, bSize, cSize;
	digit_t * a, * b, * c;
	KrkLong hi, lo, tmp;
	krk_long_init_many(&hi, &lo, &tmp, NULL);
	size_t w2 = w >> 1;
	_krk_long_rshift_z(&hi, n, w2);
	_krk_long_lshift_z(&tmp, &hi, w2);
	krk_long_sub(&lo, n, &tmp);
	krk_long_clear_many(&tmp, NULL);
	a = long_to_dec_inner(&hi, w - w2, &aSize);
	krk_long_clear_many(&hi, NULL);
	b = dec_two_raised(w2, &bSize);
	c = dec_mul(a, aSize, b, bSize, &cSize);
	free(a);
	free(b);
	a = long_to_dec_inner(&lo, w2, &aSize);
	krk_long_clear_many(&lo,NULL);
	digit_t * result = dec_add(a, aSize, c, cSize, sizeOut);
	free(a);
	free(c);
	return result;
}


static char * krk_long_to_decimal_str(const KrkLong * value, size_t * len) {
	KrkLong abs = *value;
	int inv = (krk_long_sign(&abs) == -1);
	krk_long_set_sign(&abs, 1);
	size_t w = _bits_in(&abs);
	size_t size;
	digit_t * digits = long_to_dec_inner(&abs, w, &size);
	int leading = 0;
	for (size_t j = 0, div = DEC_DIGIT_MAX/10; j < DEC_DIGIT_CNT; j++, div/=10) {
		if (((digits[size-1] / div) % 10)) break;
		leading += 1;
	}
	char * out = malloc(size * DEC_DIGIT_CNT + 1 - leading + inv);
	char * writer = out;
	if (inv) *(writer++) = '-';
	for (size_t i = 0; i < size; ++i) {
		for (size_t j = 0, div = DEC_DIGIT_MAX/10; j < DEC_DIGIT_CNT; j++, div/=10) {
			if (leading) { leading--; continue; }
			*(writer++) = ((digits[size-i-1] / div) % 10) + '0';
		}
	}
	*writer = '\0';

	free(digits);
	*len = writer - out;

	return out;
}

static size_t round_to(char * str, size_t len, size_t actual, size_t digits) {
	if (actual > digits) {
		int carry = 0;
		if (str[digits] == '5' && ((digits ? str[digits-1] : 0) % 2 == 0)) {
			int all_zeros = 1;
			for (size_t j = actual - 1; j > digits; j--) {
				if (str[j] != '0') {
					all_zeros = 0;
					break;
				}
			}
			carry = all_zeros ? 0 : 1;
		} else if (str[digits] >= '5') {
			carry = 1;
		}
		size_t i = digits;
		while (i && carry) {
			if (str[i-1] - '0' + carry > 9) {
				str[i-1] = '0';
				carry = 1;
			} else {
				str[i-1] += carry;
				carry = 0;
			}
			i--;
		}
		if (carry && i == 0) {
			for (size_t j = 0; j < digits; ++j) {
				str[j+1] = str[j];
			}
			str[0] = '1';
			return 1;
		}
	}
	return 0;
}

void krk_pushStringBuilder(struct StringBuilder * sb, char c) {
	if (sb->capacity < sb->length + 1) {
		size_t old = sb->capacity;
		sb->capacity = (old < 8) ? 8 : old * 2;
		sb->bytes = realloc(sb->bytes, sb->capacity);
	}
	sb->bytes[sb->length++] = c;
}

void krk_pushStringBuilderStr(struct StringBuilder * sb, const char *str, size_t len) {
	if (sb->capacity < sb->length + len) {
		while (sb->capacity < sb->length + len) {
			size_t old = sb->capacity;
			sb->capacity = (old < 8) ? 8 : old * 2;
		}
		sb->bytes = realloc(sb->bytes, sb->capacity);
	}
	for (size_t i = 0; i < len; ++i) {
		sb->bytes[sb->length++] = *(str++);
	}
}

static void _freeStringBuilder(struct StringBuilder * sb) {
	free(sb->bytes);
	sb->bytes = NULL;
	sb->length = 0;
	sb->capacity = 0;
}

static char * krk_finishStringBuilder(struct StringBuilder * sb) {
	char * x = malloc(sb->length + 1);
	memcpy(x, sb->bytes, sb->length);
	x[sb->length] = 0;
	_freeStringBuilder(sb);
	return x;
}

static KrkLong cached_decimals[54];
static int _init_decimals = 0;

static char * krk_double_to_string(double a, unsigned int digits, char formatter, int plus, int forcedigits) {
	union { double d; uint64_t u; } val = {.d = a};

	int noexp = (formatter | 0x20) == 'f';
	int alwaysexp = (formatter | 0x20) == 'e';
	int gmode = (formatter | 0x20) == 'g';
	int caps = !(formatter & 0x20);
	char expch = caps ? 'E' : 'e';

	int sign = (val.u >> 63ULL) ? 1 : 0;
	int64_t m = val.u & 0x000fffffffffffffULL;
	int64_t e = ((val.u >> 52ULL) & 0x7FF) - 0x3FF;
	if (e == 1024) {
		struct StringBuilder sb = {0};
		if (sign && !m) krk_pushStringBuilder(&sb, '-');
		else if (plus) krk_pushStringBuilder(&sb, '+');
		if (m) krk_pushStringBuilderStr(&sb, caps ? "NAN" : "nan", 3);
		else krk_pushStringBuilderStr(&sb, caps ? "INF" : "inf", 3);
		return krk_finishStringBuilder(&sb);
	}
	if (e == -1023 && m == 0) {
		struct StringBuilder sb = {0};
		if (sign) krk_pushStringBuilder(&sb, '-');
		else if (plus) krk_pushStringBuilder(&sb,'+');
		krk_pushStringBuilder(&sb, '0');
		if (digits && (forcedigits || gmode)) {
			krk_pushStringBuilder(&sb, '.');
			for (unsigned int i = 0; i < ((gmode) ? 1 : (digits - ((!noexp && !alwaysexp) ? 1 : 0))); ++i) {
				krk_pushStringBuilder(&sb, '0');
			}
		}
		if (alwaysexp) {
			krk_pushStringBuilder(&sb, expch);
			krk_pushStringBuilderStr(&sb, "+00", 3);
		}
		return krk_finishStringBuilder(&sb);
	}

	if (!_init_decimals) {
		_init_decimals = 1;

		/* TODO We should probably just embed static versions of these... */
		KrkLong d;
		krk_long_init_si(&d, 1);
		KrkLong x;
		krk_long_init_si(&x, 10);
		for (int i = 0; i < 52; ++i) {
			krk_long_mul(&d, &d, &x);
			if (i == 30) {
				krk_long_init_copy(&cached_decimals[53], &d);
			}
		}
		krk_long_clear(&x);

		for (int i = 0; i < 53; ++i) {
			krk_long_init_copy(&cached_decimals[i], &d);
			if (i != 52) {
				KrkLong o;
				krk_long_init_si(&o,0);
				_krk_long_rshift_z(&o,&d,1);
				d = o;
			}
		}

		krk_long_clear(&d);
	}

	KrkLong c;
	if (e == -1023) {
		krk_long_init_si(&c,0);
		e = -1022;
	} else {
		krk_long_init_copy(&c, &cached_decimals[0]);
	}

	for (int i = 0; i < 52; ++i) {
		if (m & (1ULL << (51 - i))) {
			krk_long_add(&c,&c, &cached_decimals[i+1]);
		}
	}

	int b = 52;

	if (e < 0) {
		while (1) {
			ssize_t i = 0;
			while (!_bit_is_set(&c,i)) i++;
			if (i >= -e) break;
			krk_long_mul(&c,&c,&cached_decimals[53]);
			b += 31;
		}
	}

	if (e) {
		KrkLong o;
		krk_long_init_si(&o,0);
		if (e < 0) {
			_krk_long_rshift_z(&o,&c,-e);
		} else {
			_krk_long_lshift_z(&o,&c,e);
		}
		krk_long_clear(&c);
		c = o;
	}

	size_t len = 0;
	char * str = krk_long_to_decimal_str(&c, &len);
	krk_long_clear(&c);

	size_t actual = len;
	while (actual > 1 && str[actual-1] == '0') actual--;

	int ten_exponent = (int)len - b - 1;
	int print_exponent = 0;
	int whole_digits = ((int)len >= b) ? ten_exponent + 1 : 0;
	int missing_digits = (b >= (int)len) ? b - (int)len : 0;
	int trailing_zeros = 0;

	struct StringBuilder sb = {0};

	if (sign) krk_pushStringBuilder(&sb, '-');
	else if (plus) krk_pushStringBuilder(&sb, '+');

	if (!alwaysexp && !noexp) {
		if (digits == 0) digits = 1;
		if (actual > digits) {
			int overflowed = round_to(str, len, actual, digits);
			if (overflowed) {
				ten_exponent += 1;
				if (ten_exponent) whole_digits++;
			}
			actual = digits;
		} else {
			trailing_zeros = digits - actual;
		}

		while (actual > 1 && str[actual-1] == '0') {
			actual--;
			trailing_zeros++;
		}

		if (ten_exponent < -4 || ten_exponent >= (int)digits) {
			print_exponent = 1;
			whole_digits = 1;
			missing_digits = 0;
			if (!forcedigits) trailing_zeros = 0;
		} else if (!forcedigits) {
			if (gmode && actual <= (size_t)whole_digits) trailing_zeros = 1;
			else trailing_zeros = 0;
		}
	} else if (noexp) {
		if (missing_digits > (int)digits) {
			actual = whole_digits;
			missing_digits = digits;
		} else if (missing_digits && missing_digits + actual > digits) {
			if (round_to(str, len, actual, digits - missing_digits)) missing_digits--;
			actual = digits - missing_digits;
		} else if (!missing_digits && actual > whole_digits + digits) {
			if (round_to(str, len, actual, digits + whole_digits)) whole_digits++;
			actual = digits + whole_digits;
		} else if (actual <= (size_t)whole_digits) {
			missing_digits = digits;
		} else {
			trailing_zeros = digits - (actual - whole_digits + missing_digits);
		}
	} else if (alwaysexp) {
		if (actual > digits) {
			if (round_to(str, len, actual, digits + 1)) ten_exponent += 1;
			actual = digits + 1;
		} else {
			trailing_zeros = digits + 1 - actual;
		}
		print_exponent = 1;
		whole_digits = 1;
		missing_digits = 0;
	}

	if (!whole_digits) krk_pushStringBuilder(&sb,'0');
	else krk_pushStringBuilderStr(&sb,str,whole_digits);
	if (forcedigits || actual > (size_t)whole_digits || trailing_zeros || missing_digits) krk_pushStringBuilder(&sb, '.');
	if (missing_digits) for (int i = 0; i < missing_digits; ++i) krk_pushStringBuilder(&sb, '0');
	if (actual > (size_t)whole_digits) krk_pushStringBuilderStr(&sb, str + whole_digits, actual - whole_digits);
	for (int i = 0; i < trailing_zeros; ++i) krk_pushStringBuilder(&sb, '0');

	if (print_exponent) {
		char expsign = ten_exponent < 0 ? '-' : '+';
		int abs_ten_exponent = ten_exponent < 0 ? -ten_exponent : ten_exponent;
		krk_pushStringBuilder(&sb, expch);
		krk_pushStringBuilder(&sb, expsign);
		if (abs_ten_exponent < 10) krk_pushStringBuilder(&sb, '0');
		if (abs_ten_exponent > 999) krk_pushStringBuilder(&sb, '0' + ((abs_ten_exponent / 1000) % 10));
		if (abs_ten_exponent > 99) krk_pushStringBuilder(&sb, '0' + ((abs_ten_exponent / 100) % 10));
		if (abs_ten_exponent > 9) krk_pushStringBuilder(&sb, '0' + ((abs_ten_exponent / 10) % 10));
		krk_pushStringBuilder(&sb, '0' + (abs_ten_exponent % 10));
	}

	free(str);
	return krk_finishStringBuilder(&sb);
}

#define OUT(c) do { callback(userData, (c)); written++; } while (0)

_hidden
size_t __print_double(double value, unsigned int width, int (*callback)(void*,char), void * userData, int fill_zero, int align_right, int precision, char mode, int always_sign) {
	size_t written = 0;

	/* Initial string conversion using Kuroko's internal implementation */
	char * as_str = krk_double_to_string(value, precision, mode, always_sign, 0);
	ssize_t len = strlen(as_str);

	/* Check if result was an inf or nan string */
	int is_text = 0;
	for (ssize_t i = 0; i < len; ++i) {
		if (as_str[i] == 'n' || as_str[i] == 'N') {
			is_text = 1;
			break;
		}
	}

	int skip_minus = 0;
	if (width && fill_zero && !is_text && len < width) {
		/* Leading zeros go after a sign */
		if (as_str[0] == '-') {
			skip_minus = 1;
			OUT('-');
		} else if (as_str[0] == '+') {
			skip_minus = 1;
			OUT(always_sign == 2 ? ' ' : '+');
		}
		for (ssize_t i = 0; i < width - len; ++i) OUT('0');
	} else if (width && align_right && len < width) {
		for (ssize_t i = 0; i < width - len; ++i) OUT(' ');
	}

	/* Replace leading + sign with a space if requested. */
	if (!skip_minus && always_sign == 2 && as_str[0] == '+') {
		OUT(' ');
		skip_minus = 1;
	}

	for (ssize_t i = skip_minus; i < len; ++i) OUT(as_str[i]);

	/* When left aligned, fill the rest with spaces. */
	if (width && !align_right && len < width) for (ssize_t i = 0; i < width - len; ++i) OUT(' ');

	free(as_str);
	return written;
}
