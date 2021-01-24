/* Kuroko bindings for Yutani */
#include <assert.h>
#include <toaru/yutani.h>
#include <toaru/decorations.h>
#include "kuroko/src/kuroko.h"
#include "kuroko/src/vm.h"
#include "kuroko/src/value.h"
#include "kuroko/src/object.h"

static KrkInstance * module;
static KrkInstance * yctxInstance = NULL;

#define S(c) (krk_copyString(c,sizeof(c)-1))

static KrkClass * Message;
struct MessageClass {
	KrkInstance inst;
	yutani_msg_t * msg;
};

static KrkClass * Yutani;
struct YutaniClass {
	KrkInstance inst;
	yutani_t * yctx;
};

static KrkClass * GraphicsContext;
struct GraphicsContext {
	KrkInstance inst;
	gfx_context_t * ctx;
	int doubleBuffered;
};

static KrkClass * YutaniWindow;
struct WindowClass {
	KrkInstance inst;
	gfx_context_t * ctx;
	int doubleBuffered;
	yutani_window_t * window;
};

static KrkClass * YutaniSprite;
struct YutaniSprite {
	KrkInstance inst;
	gfx_context_t * ctx;
	int doubleBuffered;
	sprite_t sprite;
};

static KrkClass * Decorator;
/* no additional fields */

static KrkClass * YutaniColor;
struct YutaniColor {
	KrkInstance inst;
	uint32_t color;
};

/**
 * Convenience wrapper to make a class and attach it to the module, while
 * handling stack push/pop to keep things from being prematurely GC'd.
 */
KrkClass * krk_createClass(KrkInstance * inModule, const char * name, KrkClass * base) {
	if (!base) base = vm.objectClass;
	KrkString * str_Name = krk_copyString(name, strlen(name));
	krk_push(OBJECT_VAL(str_Name));
	KrkClass * obj_Class = krk_newClass(str_Name, base);
	krk_push(OBJECT_VAL(obj_Class));
	krk_attachNamedObject(&inModule->fields, name, (KrkObj *)obj_Class);
	krk_pop(); /* obj_Class */
	krk_pop(); /* str_Name */

	return obj_Class;
}

#define DO_FIELD(name, body) \
	{ krk_push(OBJECT_VAL(S(name))); \
		if (krk_valuesEqual(argv[1], krk_peek(0))) { \
			krk_pop(); \
			body \
		} krk_pop(); }

#define TO_INT_(name) { return INTEGER_VAL((msg-> name)); }
#define TO_INT(name) { return INTEGER_VAL((me-> name)); }
#define WID() DO_FIELD("wid", TO_INT(wid))
#define STRUCT(type) type * me = (void*)msg->data

static void _message_sweep(KrkInstance * self) {
	free(((struct MessageClass*)self)->msg);
}

static KrkValue _message_getattr(int argc, KrkValue argv[]) {
	assert(argc == 2);
	KrkInstance * self = AS_INSTANCE(argv[0]);

	yutani_msg_t * msg = ((struct MessageClass*)self)->msg;
	if (!msg) return NONE_VAL();

	DO_FIELD("magic", TO_INT_(magic));
	DO_FIELD("type",  TO_INT_(type));
	DO_FIELD("size",  TO_INT_(size));

	switch (msg->type) {
		case YUTANI_MSG_WELCOME: {
			STRUCT(struct yutani_msg_welcome);
			DO_FIELD("display_width",  TO_INT(display_width));
			DO_FIELD("display_height", TO_INT(display_height));
		} break;
		case YUTANI_MSG_WINDOW_MOUSE_EVENT: {
			STRUCT(struct yutani_msg_window_mouse_event);
			WID();
			DO_FIELD("new_x", TO_INT(new_x));
			DO_FIELD("new_y", TO_INT(new_y));
			DO_FIELD("old_x", TO_INT(old_x));
			DO_FIELD("old_y", TO_INT(old_y));
			DO_FIELD("buttons", TO_INT(buttons));
			DO_FIELD("command", TO_INT(command));
			DO_FIELD("modifiers", TO_INT(modifiers));
		} break;
		case YUTANI_MSG_WINDOW_FOCUS_CHANGE: {
			STRUCT(struct yutani_msg_window_focus_change);
			WID();
			DO_FIELD("focused", TO_INT(focused));
		} break;
		case YUTANI_MSG_RESIZE_OFFER: {
			STRUCT(struct yutani_msg_window_resize);
			WID();
			DO_FIELD("width", TO_INT(width));
			DO_FIELD("height", TO_INT(height));
			DO_FIELD("bufid", TO_INT(bufid));
		} break;
		case YUTANI_MSG_WINDOW_ADVERTISE: {
			STRUCT(struct yutani_msg_window_advertise);
			WID();
			DO_FIELD("flags", TO_INT(flags));
			DO_FIELD("size", TO_INT(size));
			DO_FIELD("name", { char * s = me->strings + me->offsets[0]; size_t l = strlen(s); return OBJECT_VAL(krk_copyString(s,l)); });
			DO_FIELD("icon", { char * s = me->strings + me->offsets[1]; size_t l = strlen(s); return OBJECT_VAL(krk_copyString(s,l)); });
		} break;
		case YUTANI_MSG_WINDOW_MOVE: {
			STRUCT(struct yutani_msg_window_move);
			WID();
			DO_FIELD("x", TO_INT(x));
			DO_FIELD("y", TO_INT(y));
		} break;
		case YUTANI_MSG_KEY_EVENT: {
			STRUCT(struct yutani_msg_key_event);
			WID();

			DO_FIELD("keycode", TO_INT(event.keycode));
			DO_FIELD("modifiers", TO_INT(event.modifiers));
			DO_FIELD("action", TO_INT(event.action));
			DO_FIELD("key", TO_INT(event.key));

			DO_FIELD("kbd_state", TO_INT(state.kbd_state));
			DO_FIELD("kbd_s_state", TO_INT(state.kbd_s_state));
			DO_FIELD("k_ctrl", TO_INT(state.k_ctrl));
			DO_FIELD("k_shift", TO_INT(state.k_shift));
			DO_FIELD("k_alt", TO_INT(state.k_alt));
			DO_FIELD("k_super", TO_INT(state.k_super));
			DO_FIELD("kl_ctrl", TO_INT(state.kl_ctrl));
			DO_FIELD("kl_shift", TO_INT(state.kl_shift));
			DO_FIELD("kl_alt", TO_INT(state.kl_alt));
			DO_FIELD("kl_super", TO_INT(state.kl_super));
			DO_FIELD("kr_ctrl", TO_INT(state.kr_ctrl));
			DO_FIELD("kr_shift", TO_INT(state.kr_shift));
			DO_FIELD("kr_alt", TO_INT(state.kr_alt));
			DO_FIELD("kr_super", TO_INT(state.kr_super));
			DO_FIELD("kbd_esc_buf", TO_INT(state.kbd_esc_buf));
		} break;
	}

	krk_runtimeError(vm.exceptions.attributeError, "no attribute '%s'", AS_CSTRING(argv[1]));
	return NONE_VAL();
}
#undef DO_FIELD
#undef TO_INT_
#undef GET
#undef TO_INT
#undef WID

static KrkValue _yutani_init(int argc, KrkValue argv[], int hasKw) {
	if (yctxInstance) {
		krk_runtimeError(vm.exceptions.valueError, "class 'Yutani' is a singleton and has already been initialized.");
		return NONE_VAL();
	}

	KrkInstance * self = AS_INSTANCE(argv[0]);

	/* Connect and let's go. */
	yutani_t * yctx = yutani_init();
	if (!yctx) {
		krk_runtimeError(vm.exceptions.ioError, "Failed to connect to compositor.");
		return NONE_VAL();
	}

	init_decorations();

	((struct YutaniClass*)self)->yctx = yctx;
	yctxInstance = self;
	krk_attachNamedObject(&module->fields, "_yutani_t", (KrkObj*)self);

	return argv[0];
}

static KrkValue _yutani_display_width(int argc, KrkValue argv[]) {
	KrkInstance * self = AS_INSTANCE(argv[0]);
	yutani_t * ctx = ((struct YutaniClass*)self)->yctx;
	return INTEGER_VAL(ctx->display_width);
}

static KrkValue _yutani_display_height(int argc, KrkValue argv[]) {
	KrkInstance * self = AS_INSTANCE(argv[0]);
	yutani_t * ctx = ((struct YutaniClass*)self)->yctx;
	return INTEGER_VAL(ctx->display_height);
}

static KrkValue _yutani_poll(int argc, KrkValue argv[], int hasKw) {
	KrkInstance * self = AS_INSTANCE(argv[0]);

	int sync = (argc > 1 && IS_BOOLEAN(argv[1])) ? AS_BOOLEAN(argv[1]) : 1;
	yutani_msg_t * result;
	if (sync) {
		result = yutani_poll(((struct YutaniClass*)self)->yctx);
	} else {
		result = yutani_poll_async(((struct YutaniClass*)self)->yctx);
	}

	if (!result) return NONE_VAL();

	KrkInstance * out = krk_newInstance(Message);
	krk_push(OBJECT_VAL(out));
	((struct MessageClass*)out)->msg = result;

	return krk_pop();
}

static KrkValue _yutani_wait_for(int argc, KrkValue argv[]) {
	if (argc != 2) { krk_runtimeError(vm.exceptions.argumentError, "Expected two arguments"); return NONE_VAL(); }
	KrkInstance * self = AS_INSTANCE(argv[0]);
	yutani_msg_t * result = yutani_wait_for(((struct YutaniClass*)self)->yctx, AS_INTEGER(argv[1]));
	KrkInstance * out = krk_newInstance(Message);
	krk_push(OBJECT_VAL(out));
	((struct MessageClass*)out)->msg = result;

	return krk_pop();
}

#define GET_ARG(p,name,type) do { \
	if (hasKw && krk_tableGet(AS_DICT(argv[argc]), OBJECT_VAL(S(#name)), &name)) { \
		if (!krk_isInstanceOf(name,type)) \
			return krk_runtimeError(vm.exceptions.typeError, #name " argument should be " #type ", not '%s'", krk_typeName(name)); \
	} else if (argc > p) { \
		name = argv[p]; \
		if (!krk_isInstanceOf(name,type)) \
			return krk_runtimeError(vm.exceptions.typeError, #name " argument should be " #type ", not '%s'", krk_typeName(name)); \
	} \
} while (0)

#define GFX_PROPERTY(name) \
static KrkValue _gfx_ ## name (int argc, KrkValue argv[]) { \
	if (argc != 1 || !krk_isInstanceOf(argv[0], GraphicsContext)) \
		return krk_runtimeError(vm.exceptions.typeError, "Expected GraphicsContext"); \
	struct GraphicsContext * self = (struct GraphicsContext *)AS_INSTANCE(argv[0]); \
	return INTEGER_VAL(self->ctx-> name); \
}

GFX_PROPERTY(width);
GFX_PROPERTY(height);

#define CHECK_GFX() \
	if (argc < 1 || !krk_isInstanceOf(argv[0], GraphicsContext)) \
		return krk_runtimeError(vm.exceptions.typeError, "expected GraphicsContext"); \
	struct GraphicsContext * self = (struct GraphicsContext*)AS_INSTANCE(argv[0])

static KrkValue _gfx_fill(int argc, KrkValue argv[]) {
	CHECK_GFX();
	if (argc < 2 || !krk_isInstanceOf(argv[1], YutaniColor))
		return krk_runtimeError(vm.exceptions.typeError, "fill() takes one color() argument");
	struct YutaniColor * color = (struct YutaniColor*)AS_INSTANCE(argv[1]);
	draw_fill(self->ctx, color->color);
	return NONE_VAL();
}

static KrkValue _gfx_flip(int argc, KrkValue argv[]) {
	CHECK_GFX();
	if (self->doubleBuffered) {
		flip(self->ctx);
	}
	return NONE_VAL();
}

static KrkValue _gfx_blur(int argc, KrkValue argv[]) {
	CHECK_GFX();
	int radius = 2;
	if (argc > 1 && IS_INTEGER(argv[1])) radius = AS_INTEGER(argv[1]);
	else if (argc > 1) return krk_runtimeError(vm.exceptions.typeError, "expected int");
	blur_context_box(self->ctx, radius);
	return NONE_VAL();
}

static KrkValue _gfx_line(int argc, KrkValue argv[]) {
	CHECK_GFX();
	if (argc < 6 ||
		!IS_INTEGER(argv[1]) ||
		!IS_INTEGER(argv[2]) ||
		!IS_INTEGER(argv[3]) ||
		!IS_INTEGER(argv[4]) ||
		!krk_isInstanceOf(argv[5], YutaniColor)) {
		return krk_runtimeError(vm.exceptions.typeError, "line() expects 4 ints and a color");
	}

	int32_t x0 = AS_INTEGER(argv[1]);
	int32_t x1 = AS_INTEGER(argv[2]);
	int32_t y0 = AS_INTEGER(argv[3]);
	int32_t y1 = AS_INTEGER(argv[4]);
	struct YutaniColor * color = (struct YutaniColor*)AS_INSTANCE(argv[5]);

	if (argc > 6) {
		if (IS_INTEGER(argv[6])) {
			draw_line_thick(self->ctx,x0,x1,y0,y1,color->color,AS_INTEGER(argv[6]));
		} else if (IS_FLOATING(argv[6])) {
			draw_line_aa(self->ctx,x0,x1,y0,y1,color->color,AS_FLOATING(argv[6]));
		} else {
			return krk_runtimeError(vm.exceptions.typeError, "thickness must be int or float, not '%s'", krk_typeName(argv[6]));
		}
	} else {
		draw_line(self->ctx,x0,x1,y0,y1,color->color);
	}

	return NONE_VAL();
}

static KrkValue _gfx_rect(int argc, KrkValue argv[], int hasKw) {
	if (hasKw) argc--;
	CHECK_GFX();

	if (argc != 6 ||
		!IS_INTEGER(argv[1]) ||
		!IS_INTEGER(argv[2]) ||
		!IS_INTEGER(argv[3]) ||
		!IS_INTEGER(argv[4]) ||
		!krk_isInstanceOf(argv[5], YutaniColor)) {
		return krk_runtimeError(vm.exceptions.typeError, "rect() expects 4 ints and a color");
	}

	int32_t x = AS_INTEGER(argv[1]);
	int32_t y = AS_INTEGER(argv[2]);
	uint16_t width = AS_INTEGER(argv[3]);
	uint16_t height = AS_INTEGER(argv[4]);
	struct YutaniColor * color = (struct YutaniColor*)AS_INSTANCE(argv[5]);

	KrkValue solid = BOOLEAN_VAL(0), radius = NONE_VAL();
	if (hasKw) {
		krk_tableGet(AS_DICT(argv[argc]), OBJECT_VAL(S("solid")), &solid);
		krk_tableGet(AS_DICT(argv[argc]), OBJECT_VAL(S("radius")), &radius);
	}

	if (!IS_BOOLEAN(solid))
		return krk_runtimeError(vm.exceptions.typeError, "solid must be bool");
	if (!IS_NONE(radius) && !IS_INTEGER(radius))
		return krk_runtimeError(vm.exceptions.typeError, "radius must be int");
	if (!IS_INTEGER(radius) && AS_BOOLEAN(solid))
		return krk_runtimeError(vm.exceptions.typeError, "radius and solid can not be used together");

	if (AS_BOOLEAN(solid)) {
		draw_rectangle_solid(self->ctx, x, y, width, height, color->color);
	} else if (IS_INTEGER(radius)) {
		draw_rounded_rectangle(self->ctx, x, y, width, height, AS_INTEGER(radius), color->color);
	} else {
		draw_rectangle(self->ctx, x, y, width, height, color->color);
	}

	return NONE_VAL();
}

static KrkValue _gfx_draw_sprite(int argc, KrkValue argv[], int hasKw) {
	if (hasKw) argc--;
	CHECK_GFX();

	if (argc < 2 || !krk_isInstanceOf(argv[1], YutaniSprite))
		return krk_runtimeError(vm.exceptions.typeError, "expected Sprite");

	if (argc < 4 || !IS_INTEGER(argv[2]) || !IS_INTEGER(argv[3]))
		return krk_runtimeError(vm.exceptions.typeError, "expected integer coordinate pair");

	/* Potential kwargs: rotation:float, alpha:float, scale:(int,int)... */
	KrkValue rotation = NONE_VAL(), alpha = NONE_VAL(), scale=NONE_VAL(), color=NONE_VAL();
	if (hasKw) {
		krk_tableGet(AS_DICT(argv[argc]), OBJECT_VAL(S("alpha")), &alpha);
		krk_tableGet(AS_DICT(argv[argc]), OBJECT_VAL(S("rotation")), &rotation);
		krk_tableGet(AS_DICT(argv[argc]), OBJECT_VAL(S("scale")), &scale);
		krk_tableGet(AS_DICT(argv[argc]), OBJECT_VAL(S("color")), &color);
	}

	if (!IS_NONE(alpha) && !IS_FLOATING(alpha))
		return krk_runtimeError(vm.exceptions.typeError, "alpha must be float");
	if (!IS_NONE(rotation) && !IS_FLOATING(rotation))
		return krk_runtimeError(vm.exceptions.typeError, "rotation must be float");
	if (!IS_NONE(color) && !krk_isInstanceOf(color,YutaniColor))
		return krk_runtimeError(vm.exceptions.typeError, "color must be color");
	if (!IS_NONE(scale) && (!IS_TUPLE(scale) || AS_TUPLE(scale)->values.count != 2 ||
		!IS_INTEGER(AS_TUPLE(scale)->values.values[0]) ||
		!IS_INTEGER(AS_TUPLE(scale)->values.values[1])))
		return krk_runtimeError(vm.exceptions.typeError, "scale must be 2-tuple of ints");
	if (!IS_NONE(rotation) + !IS_NONE(scale) + !IS_NONE(color) > 1)
		return krk_runtimeError(vm.exceptions.typeError, "can not combine rotation / scale / color");

	if ((!IS_NONE(rotation) || !IS_NONE(color)) && IS_NONE(alpha))
		alpha = FLOATING_VAL(1.0);

	struct YutaniSprite * sprite = (struct YutaniSprite*)AS_INSTANCE(argv[1]);
	int32_t x = AS_INTEGER(argv[2]);
	int32_t y = AS_INTEGER(argv[3]);

	if (!IS_NONE(scale)) {
		int32_t width = AS_INTEGER(AS_TUPLE(scale)->values.values[0]);
		int32_t height = AS_INTEGER(AS_TUPLE(scale)->values.values[1]);
		if (IS_NONE(alpha)) {
			draw_sprite_scaled(self->ctx, &sprite->sprite, x, y, width, height);
		} else {
			draw_sprite_scaled_alpha(self->ctx, &sprite->sprite, x, y, width, height, AS_FLOATING(alpha));
		}
	} else if (IS_NONE(alpha)) {
		draw_sprite(self->ctx, &sprite->sprite, x, y);
	} else if (!IS_NONE(color)) {
		draw_sprite_alpha_paint(self->ctx, &sprite->sprite, x, y, AS_FLOATING(alpha), ((struct YutaniColor*)AS_INSTANCE(color))->color);
	} else if (!IS_NONE(rotation)) {
		draw_sprite_rotate(self->ctx, &sprite->sprite, x, y, AS_FLOATING(rotation), AS_FLOATING(alpha));
	} else {
		draw_sprite_alpha(self->ctx, &sprite->sprite, x, y, AS_FLOATING(alpha));
	}

	return NONE_VAL();
}

static void _sprite_sweep(KrkInstance * self) {
	struct YutaniSprite * sprite = (struct YutaniSprite*)self;

	if (sprite->sprite.masks) free(sprite->sprite.masks);
	if (sprite->sprite.bitmap) free(sprite->sprite.bitmap);
	if (sprite->ctx) free(sprite->ctx);
}

static KrkValue _sprite_init(int argc, KrkValue argv[]) {
	if (argc < 1 || !krk_isInstanceOf(argv[0], YutaniSprite))
		return krk_runtimeError(vm.exceptions.typeError, "expected sprite");

	if (argc < 2 || !IS_STRING(argv[1]))
		return krk_runtimeError(vm.exceptions.typeError, "Sprite() takes one str argument");

	struct YutaniSprite * self = (struct YutaniSprite*)AS_INSTANCE(argv[0]);

	int result = load_sprite(&self->sprite, AS_CSTRING(argv[1]));
	if (result) {
		return krk_runtimeError(vm.exceptions.ioError, "Sprite() could not be initialized");
	}

	self->ctx = init_graphics_sprite(&self->sprite);

	return argv[0];
}

static KrkValue _window_init(int argc, KrkValue argv[], int hasKw) {
	if (!yctxInstance) return krk_runtimeError(vm.exceptions.valueError, "Compositor is not initialized");
	if (argc < 1 || !krk_isInstanceOf(argv[0], YutaniWindow))
		return krk_runtimeError(vm.exceptions.typeError, "Failed to initialize window");

	if (argc < 3 || !IS_INTEGER(argv[1]) || !IS_INTEGER(argv[2]))
		return krk_runtimeError(vm.exceptions.argumentError, "Expected at least two (integer) arguments (width, height)");

	if (hasKw) argc--;

	KrkInstance * _self = AS_INSTANCE(argv[0]);
	struct WindowClass * self = (struct WindowClass*)_self;
	krk_integer_type width = AS_INTEGER(argv[1]);
	krk_integer_type height = AS_INTEGER(argv[2]);

	KrkValue flags = INTEGER_VAL(0), title = NONE_VAL(), icon = NONE_VAL(), doublebuffer = BOOLEAN_VAL(0);
	GET_ARG(3, flags, vm.baseClasses.intClass);
	GET_ARG(4, title, vm.baseClasses.strClass);
	GET_ARG(5, icon,  vm.baseClasses.strClass);
	GET_ARG(6, doublebuffer, vm.baseClasses.boolClass);

	self->window = yutani_window_create_flags(((struct YutaniClass*)yctxInstance)->yctx,
		width, height, AS_INTEGER(flags));

	self->doubleBuffered = AS_BOOLEAN(doublebuffer);

	if (self->doubleBuffered) {
		self->ctx = init_graphics_yutani_double_buffer(self->window);
	} else {
		self->ctx = init_graphics_yutani(self->window);
	}

	if (!IS_NONE(title)) {
		if (!IS_NONE(icon)) {
			yutani_window_advertise_icon(((struct YutaniClass*)yctxInstance)->yctx, self->window, AS_CSTRING(title), AS_CSTRING(icon));
		} else {
			yutani_window_advertise(((struct YutaniClass*)yctxInstance)->yctx, self->window, AS_CSTRING(title));
		}
	}

	krk_attachNamedValue(&_self->fields, "title", title);
	krk_attachNamedValue(&_self->fields, "icon", icon);
	krk_attachNamedValue(&_self->fields, "closed", BOOLEAN_VAL(0));

	return argv[0];
}

static KrkValue _window_flip(int argc, KrkValue argv[]) {
	if (argc < 1 || !krk_isInstanceOf(argv[0], YutaniWindow))
		return krk_runtimeError(vm.exceptions.typeError, "expected window");
	KrkInstance * _self = AS_INSTANCE(argv[0]);
	struct WindowClass * self = (struct WindowClass*)_self;
	if (self->doubleBuffered) {
		flip(self->ctx);
	}
	yutani_flip(((struct YutaniClass*)yctxInstance)->yctx, self->window);
	return NONE_VAL();
}

static KrkValue _window_move(int argc, KrkValue argv[]) {
	if (argc < 1 || !krk_isInstanceOf(argv[0], YutaniWindow))
		return krk_runtimeError(vm.exceptions.typeError, "expected window");
	if (argc < 3 || !IS_INTEGER(argv[1]) || !IS_INTEGER(argv[2]))
		return krk_runtimeError(vm.exceptions.typeError, "expected two integer arguments");
	KrkInstance * _self = AS_INSTANCE(argv[0]);
	struct WindowClass * self = (struct WindowClass*)_self;
	yutani_window_move(((struct YutaniClass*)yctxInstance)->yctx, self->window, AS_INTEGER(argv[1]), AS_INTEGER(argv[2]));
	return NONE_VAL();
}

static KrkValue _window_set_focused(int argc, KrkValue argv[]) {
	if (argc < 1 || !krk_isInstanceOf(argv[0], YutaniWindow))
		return krk_runtimeError(vm.exceptions.typeError, "expected window");
	if (argc < 2 || !IS_INTEGER(argv[1]))
		return krk_runtimeError(vm.exceptions.typeError, "expected integer argument");
	KrkInstance * _self = AS_INSTANCE(argv[0]);
	struct WindowClass * self = (struct WindowClass*)_self;
	self->window->focused = AS_INTEGER(argv[1]);
	return NONE_VAL();
}

#define WINDOW_PROPERTY(name) \
static KrkValue _window_ ## name (int argc, KrkValue argv[]) { \
	if (argc != 1 || !krk_isInstanceOf(argv[0], YutaniWindow)) \
		return krk_runtimeError(vm.exceptions.typeError, "Expected Window"); \
	struct WindowClass * self = (struct WindowClass*)AS_INSTANCE(argv[0]); \
	return INTEGER_VAL(self->window-> name); \
}

WINDOW_PROPERTY(wid);
WINDOW_PROPERTY(focused);

static KrkValue _decor_get_bounds(int argc, KrkValue argv[]) {
	if (argc > 0 && !krk_isInstanceOf(argv[0], YutaniWindow))
		return krk_runtimeError(vm.exceptions.typeError, "expected window");
	struct decor_bounds bounds;

	decor_get_bounds((argc > 0) ? ((struct WindowClass*)AS_INSTANCE(argv[0]))->window : NULL,
		&bounds);

	KRK_PAUSE_GC();

	KrkValue result = krk_dict_of(6 * 2, (KrkValue[]) {
		OBJECT_VAL(S("top_height")),    INTEGER_VAL(bounds.top_height),
		OBJECT_VAL(S("bottom_height")), INTEGER_VAL(bounds.bottom_height),
		OBJECT_VAL(S("left_width")),    INTEGER_VAL(bounds.left_width),
		OBJECT_VAL(S("right_width")),   INTEGER_VAL(bounds.right_width),
		OBJECT_VAL(S("width")),         INTEGER_VAL(bounds.width),
		OBJECT_VAL(S("height")),        INTEGER_VAL(bounds.height)
	});

	KRK_RESUME_GC();

	return result;
}

static KrkValue _decor_handle_event(int argc, KrkValue argv[]) {
	if (argc < 1 || !krk_isInstanceOf(argv[0], Message))
		return krk_runtimeError(vm.exceptions.typeError, "expected message");
	return INTEGER_VAL(decor_handle_event(((struct YutaniClass*)yctxInstance)->yctx, ((struct MessageClass*)AS_INSTANCE(argv[0]))->msg));
}

static KrkValue _decor_render(int argc, KrkValue argv[]) {
	if (argc < 1 || !krk_isInstanceOf(argv[0], YutaniWindow))
		return krk_runtimeError(vm.exceptions.typeError, "expected window");
	char * title = (argc > 1 && IS_STRING(argv[1])) ? AS_CSTRING(argv[1]) : NULL;
	if (title == NULL) {
		KrkValue winTitle;
		if (!krk_tableGet(&AS_INSTANCE(argv[0])->fields, OBJECT_VAL(S("title")), &winTitle) || !IS_STRING(winTitle)) {
			title = "";
		} else {
			title = AS_CSTRING(winTitle);
		}
	}
	render_decorations(((struct WindowClass*)AS_INSTANCE(argv[0]))->window,
		((struct WindowClass*)AS_INSTANCE(argv[0]))->ctx, title);
	return NONE_VAL();
}

extern void krk_dumpStack(void);

static KrkValue _yutani_color_init(int argc, KrkValue argv[]) {
	if (argc < 4 || !IS_INTEGER(argv[1]) || !IS_INTEGER(argv[2]) || !IS_INTEGER(argv[3]) ||
	   (argc > 5) || (argc == 5 && !IS_INTEGER(argv[4]))) return krk_runtimeError(vm.exceptions.typeError, "color() expects three or four integer arguments");
	if (!krk_isInstanceOf(argv[0], YutaniColor)) return krk_runtimeError(vm.exceptions.typeError, "expected color [__init__], not '%s'", krk_typeName(argv[0]));
	struct YutaniColor * self = (struct YutaniColor*)AS_INSTANCE(argv[0]);
	if (argc == 5) {
		self->color = rgba(AS_INTEGER(argv[1]),AS_INTEGER(argv[2]),AS_INTEGER(argv[3]),AS_INTEGER(argv[4]));
	} else {
		self->color = rgb(AS_INTEGER(argv[1]),AS_INTEGER(argv[2]),AS_INTEGER(argv[3]));
	}
	return argv[0];
}

static KrkValue _yutani_color_repr(int argc, KrkValue argv[]) {
	if (argc != 1 || !krk_isInstanceOf(argv[0], YutaniColor)) return krk_runtimeError(vm.exceptions.typeError, "expected color [__repr__], not '%s'", krk_typeName(argv[0]));
	struct YutaniColor * self = (struct YutaniColor*)AS_INSTANCE(argv[0]);
	char tmp[30];
	if (_ALP(self->color) != 255) {
		sprintf(tmp, "color<#%02x%02x%02x%02x>", (int)_RED(self->color), (int)_GRE(self->color), (int)_BLU(self->color), (int)_ALP(self->color));
	} else {
		sprintf(tmp, "color<#%02x%02x%02x>", (int)_RED(self->color), (int)_GRE(self->color), (int)_BLU(self->color));
	}
	return OBJECT_VAL(krk_copyString(tmp,strlen(tmp)));
}

static KrkValue _yutani_color_str(int argc, KrkValue argv[]) {
	if (argc != 1 || !krk_isInstanceOf(argv[0], YutaniColor)) return krk_runtimeError(vm.exceptions.typeError, "expected color [__str__], not '%s'", krk_typeName(argv[0]));
	struct YutaniColor * self = (struct YutaniColor*)AS_INSTANCE(argv[0]);
	char tmp[30];
	if (_ALP(self->color) != 255) {
		sprintf(tmp, "#%02x%02x%02x%02x", (int)_RED(self->color), (int)_GRE(self->color), (int)_BLU(self->color), (int)_ALP(self->color));
	} else {
		sprintf(tmp, "#%02x%02x%02x", (int)_RED(self->color), (int)_GRE(self->color), (int)_BLU(self->color));
	}
	return OBJECT_VAL(krk_copyString(tmp,strlen(tmp)));
}

KrkValue krk_module_onload__yutani(void) {
	module = krk_newInstance(vm.moduleClass);
	/* Store it on the stack for now so we can do stuff that may trip GC
	 * and not lose it to garbage colletion... */
	krk_push(OBJECT_VAL(module));

	/**
	 * class Message(object):
	 *     MSG_... = ... # Directly from the library headers.
	 */
	Message = krk_createClass(module, "Message", NULL);
	Message->allocSize = sizeof(struct MessageClass);
	Message->_ongcsweep = _message_sweep;
	/* All the MSG_ constants */
#define TYPE(type) krk_attachNamedValue(&Message->fields, "MSG_" #type, INTEGER_VAL(YUTANI_MSG_ ## type))
	TYPE(HELLO); TYPE(WINDOW_NEW); TYPE(FLIP); TYPE(KEY_EVENT); TYPE(MOUSE_EVENT);
	TYPE(WINDOW_MOVE); TYPE(WINDOW_CLOSE); TYPE(WINDOW_SHOW); TYPE(WINDOW_HIDE);
	TYPE(WINDOW_STACK); TYPE(WINDOW_FOCUS_CHANGE); TYPE(WINDOW_MOUSE_EVENT);
	TYPE(FLIP_REGION); TYPE(WINDOW_NEW_FLAGS); TYPE(RESIZE_REQUEST);
	TYPE(RESIZE_OFFER); TYPE(RESIZE_ACCEPT); TYPE(RESIZE_BUFID); TYPE(RESIZE_DONE);
	TYPE(WINDOW_ADVERTISE); TYPE(SUBSCRIBE); TYPE(UNSUBSCRIBE); TYPE(NOTIFY);
	TYPE(QUERY_WINDOWS); TYPE(WINDOW_FOCUS); TYPE(WINDOW_DRAG_START); TYPE(WINDOW_WARP_MOUSE);
	TYPE(WINDOW_SHOW_MOUSE); TYPE(WINDOW_RESIZE_START); TYPE(SESSION_END);
	TYPE(KEY_BIND); TYPE(WINDOW_UPDATE_SHAPE); TYPE(CLIPBOARD); TYPE(GOODBYE);
	TYPE(SPECIAL_REQUEST); TYPE(WELCOME); TYPE(WINDOW_INIT);
#undef TYPE
	/* Structure bindings */
	krk_defineNative(&Message->methods, ".__getattr__", _message_getattr);
	krk_finalizeClass(Message);

	YutaniColor = krk_createClass(module, "color", NULL);
	YutaniColor->allocSize = sizeof(struct YutaniColor);
	krk_defineNative(&YutaniColor->methods, ".__init__", _yutani_color_init);
	krk_defineNative(&YutaniColor->methods, ".__repr__", _yutani_color_repr);
	krk_defineNative(&YutaniColor->methods, ".__str__", _yutani_color_str);
	krk_finalizeClass(YutaniColor);

	/**
	 * class Yutani(object):
	 *     yctx = yutani_t *
	 *     display_width  = yctx->display_width
	 *     display_height = yctx->display_height
	 *
	 * def __init__(self): # Call yutani_init()
	 */
	Yutani = krk_createClass(module, "Yutani", NULL);
	Yutani->allocSize = sizeof(struct YutaniClass);
	krk_defineNative(&Yutani->methods, ":display_width", _yutani_display_width);
	krk_defineNative(&Yutani->methods, ":display_height", _yutani_display_height);
	krk_defineNative(&Yutani->methods, ".__init__", _yutani_init);
	krk_defineNative(&Yutani->methods, ".poll", _yutani_poll);
	krk_defineNative(&Yutani->methods, ".wait_for", _yutani_wait_for);
	#if 0
	krk_defineNative(&Yutani->methods, ".subscribe", _yutani_subscribe);
	krk_defineNative(&Yutani->methods, ".unsubscribe", _yutani_unsubscribe);
	krk_defineNative(&Yutani->methods, ".query_windows", _yutani_query_windows);
	krk_defineNative(&Yutani->methods, ".focus_window", _yutani_focus_window);
	krk_defineNative(&Yutani->methods, ".session_end", _yutani_session_end);
	krk_defineNative(&Yutani->methods, ".key_bind", _yutani_key_bind);
	krk_defineNative(&Yutani->methods, ".query", _yutani_query);
	krk_defineNative(&Yutani->methods, ".fileno", _yutani_fileno);
	#endif
	krk_finalizeClass(Yutani);

	GraphicsContext = krk_createClass(module, "GraphicsContext", NULL);
	GraphicsContext->allocSize = sizeof(struct GraphicsContext);
	/* No initializers for now until I bother with them... */
	krk_defineNative(&GraphicsContext->methods, ":width", _gfx_width);
	krk_defineNative(&GraphicsContext->methods, ":height", _gfx_height);
	krk_defineNative(&GraphicsContext->methods, ".fill", _gfx_fill);
	krk_defineNative(&GraphicsContext->methods, ".flip", _gfx_flip);
	krk_defineNative(&GraphicsContext->methods, ".blur", _gfx_blur);
	krk_defineNative(&GraphicsContext->methods, ".line", _gfx_line);
	krk_defineNative(&GraphicsContext->methods, ".rect", _gfx_rect);
	krk_defineNative(&GraphicsContext->methods, ".draw_sprite", _gfx_draw_sprite);
	krk_finalizeClass(GraphicsContext);

	YutaniWindow = krk_createClass(module, "Window", GraphicsContext);
	YutaniWindow->allocSize = sizeof(struct WindowClass);
	krk_defineNative(&YutaniWindow->methods, ".__init__", _window_init);
	krk_defineNative(&YutaniWindow->methods, ".flip", _window_flip);
	krk_defineNative(&YutaniWindow->methods, ".move", _window_move);
	krk_defineNative(&YutaniWindow->methods, ".set_focused", _window_set_focused);
	/* Properties */
	krk_defineNative(&YutaniWindow->methods, ":wid", _window_wid);
	krk_defineNative(&YutaniWindow->methods, ":focused", _window_focused);
	krk_finalizeClass(YutaniWindow);

	YutaniSprite = krk_createClass(module, "Sprite", GraphicsContext);
	YutaniSprite->allocSize = sizeof(struct YutaniSprite);
	YutaniSprite->_ongcsweep = _sprite_sweep;
	krk_defineNative(&YutaniSprite->methods, ".__init__", _sprite_init);
	krk_finalizeClass(YutaniSprite);

	Decorator = krk_createClass(module, "Decorator", NULL);
	krk_defineNative(&Decorator->fields, "get_bounds", _decor_get_bounds);
	krk_defineNative(&Decorator->fields, "render", _decor_render);
	krk_defineNative(&Decorator->fields, "handle_event", _decor_handle_event);
	krk_finalizeClass(Decorator);


	/**
	 * class MsgKeyEvent(Message):
	 *     type = Message.MSG_KEY_EVENT
	 */
	//KrkClass * MsgKeyEvent = krk_createClass(module, "MsgKeyEvent", Message);

	/* Pop the module object before returning; it'll get pushed again
	 * by the VM before the GC has a chance to run, so it's safe. */
	assert(AS_INSTANCE(krk_pop()) == module);
	return OBJECT_VAL(module);
}

