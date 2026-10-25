/**
 * @brief Kuroko wrapper module for libtoaru_inflate
 *
 * Provides basic decompression methods.
 */
#include <toaru/inflate.h>
#include <kuroko/kuroko.h>
#include <kuroko/vm.h>
#include <kuroko/util.h>

struct input_context {
	KrkBytes * buf;
	size_t i;
};

static uint8_t _get(struct inflate_context * ctx) {
	struct input_context * input = ctx->input_priv;
	if (input->i >= input->buf->length) return 0;
	return input->buf->bytes[input->i++];
}

static void _write(struct inflate_context * ctx, unsigned int sym) {
	krk_pushStringBuilder(ctx->output_priv, (char)sym);
}

KRK_Function(gunzip) {
	KrkBytes * buf;
	int raw = 0;
	if (!krk_parseArgs("O!|p", (const char*[]){"buf","raw"},
		KRK_BASE_CLASS(bytes), &buf, &raw)) return NONE_VAL();

	struct StringBuilder output = {0};
	struct input_context input = {buf, 0};

	struct inflate_context ctx;
	ctx.input_priv = &input;
	ctx.output_priv = &output;
	ctx.get_input = _get;
	ctx.write_output = _write;
	ctx.ring = NULL;

	if ((raw ? deflate_decompress : gzip_decompress)(&ctx)) {
		krk_discardStringBuilder(&output);
		return krk_runtimeError(vm.exceptions->valueError, "unspecified gzip error");
	}

	return krk_finishStringBuilderBytes(&output);
}

static uint8_t _get_streamed(struct inflate_context * ctx) {
	KrkValue * reader = ctx->input_priv;
	krk_push(*reader);
	KrkValue resp = krk_callStack(0);
	if (unlikely(krk_currentThread.flags & KRK_THREAD_HAS_EXCEPTION)) return 0;
	if (!IS_INTEGER(resp)) return 0;
	return (uint8_t)AS_INTEGER(resp);
}

static void _write_streamed(struct inflate_context * ctx, unsigned int sym) {
	KrkValue * writer = ctx->output_priv;
	krk_push(*writer);
	krk_push(INTEGER_VAL(sym));
	krk_callStack(1);
}

KRK_Function(gunzip_streamed) {
	KrkValue reader;
	KrkValue writer;
	int raw = 0;

	if (!krk_parseArgs("VV|p", (const char*[]){"reader","writer","raw"},
		&reader, &writer, &raw)) return NONE_VAL();

	struct inflate_context ctx;
	ctx.input_priv = &reader;
	ctx.output_priv = &writer;
	ctx.get_input = _get_streamed;
	ctx.write_output = _write_streamed;
	ctx.ring = NULL;

	if ((raw ? deflate_decompress : gzip_decompress)(&ctx)) {
		return krk_runtimeError(vm.exceptions->valueError, "unspecified gzip error");
	}

	return NONE_VAL();
}

struct BufferedContext {
	size_t cursor_input;
	size_t avail_input;
	char * buffer_input; /* pointer into bytes */
	KrkValue fill_input;

	size_t cursor_output;
	size_t buffer_size_output;
	KrkValue write_output;

	KrkTuple * gc_list;
	struct StringBuilder sb;
};

static uint8_t _get_buffered(struct inflate_context * ctx) {
	struct BufferedContext * bctx = ctx->input_priv;

	if (bctx->cursor_input == bctx->avail_input) {
		krk_push(bctx->fill_input);
		KrkValue resp = krk_callStack(0);
		bctx->gc_list->values.values[2] = resp;
		if (unlikely(krk_currentThread.flags & KRK_THREAD_HAS_EXCEPTION)) return 0;
		if (!IS_BYTES(resp)) return 0;
		if (AS_BYTES(resp)->length == 0) return 0;

		bctx->buffer_input = (char*)AS_BYTES(resp)->bytes;
		bctx->avail_input = AS_BYTES(resp)->length;
		bctx->cursor_input = 0;
	}

	return bctx->buffer_input[bctx->cursor_input++];
}

static void _buffer_flush(struct BufferedContext * bctx) {
	if (!bctx->cursor_output) return;

	krk_push(bctx->write_output);
	krk_push(krk_finishStringBuilderBytes(&bctx->sb));
	krk_callStack(1);
	bctx->cursor_output = 0;
}

static void _write_buffered(struct inflate_context * ctx, unsigned int sym) {
	struct BufferedContext * bctx = ctx->output_priv;

	krk_pushStringBuilder(&bctx->sb, (uint8_t)sym);
	bctx->cursor_output++;

	if (bctx->cursor_output == bctx->buffer_size_output) _buffer_flush(bctx);
}

KRK_Function(gunzip_buffered) {
	struct BufferedContext bctx = {0};
	bctx.buffer_size_output = 1024;
	int raw = 0;

	if (!krk_parseArgs("VV|Np", (const char *[]){"fill_input","write_output","bufsiz","raw"},
		&bctx.fill_input,
		&bctx.write_output,
		&bctx.buffer_size_output,
		&raw)) return NONE_VAL();

	if (bctx.buffer_size_output == 0) return krk_runtimeError(vm.exceptions->valueError, "bufsize must be at least 1");

	struct inflate_context ctx;
	ctx.input_priv = &bctx;
	ctx.output_priv = &bctx;
	ctx.get_input = _get_buffered;
	ctx.write_output = _write_buffered;
	ctx.ring = NULL;

	bctx.gc_list = krk_newTuple(3);
	krk_push(OBJECT_VAL(bctx.gc_list));

	bctx.gc_list->values.values[bctx.gc_list->values.count++] = bctx.fill_input;
	bctx.gc_list->values.values[bctx.gc_list->values.count++] = bctx.write_output;
	bctx.gc_list->values.values[bctx.gc_list->values.count++] = NONE_VAL();

	if ((raw ? deflate_decompress : gzip_decompress)(&ctx)) {
		krk_discardStringBuilder(&bctx.sb);
		krk_runtimeError(vm.exceptions->valueError, "unspecified gzip error");
		goto _cleanup;
	}

	_buffer_flush(&bctx);

_cleanup:
	krk_pop(); /* magic list */
	return NONE_VAL();
}

KRK_Module(_inflate) {
	BIND_FUNC(module, gunzip);
	BIND_FUNC(module, gunzip_streamed);
	BIND_FUNC(module, gunzip_buffered);
}
