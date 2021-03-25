/* Kuroko bindings for Yutani */
#include <assert.h>
#include <toaru/yutani.h>
#include <toaru/decorations.h>
#include <toaru/sdf.h>
#include <toaru/menu.h>
#include <kuroko/kuroko.h>
#include <kuroko/vm.h>
#include <kuroko/value.h>
#include <kuroko/object.h>

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

static KrkClass * YutaniColor;
struct YutaniColor {
	KrkInstance inst;
	uint32_t color;
};

static KrkClass * YutaniFont;
struct YutaniFont {
	KrkInstance inst;
	int fontType;
	int fontSize;
	double fontGamma;
	double fontStroke;
	uint32_t fontColor;
};

static KrkClass * MenuBarClass;
struct MenuBarClass {
	KrkInstance inst;
	struct menu_bar menuBar;
};

static KrkClass * MenuListClass;
struct MenuListClass {
	KrkInstance inst;
	struct MenuList * menuList;
};

static KrkClass * MenuEntryClass;
struct MenuEntryClass {
	KrkInstance inst;
	struct MenuEntry * menuEntry;
};

static KrkClass * MenuEntrySubmenuClass;
static KrkClass * MenuEntrySeparatorClass;

/**
 * Convenience wrapper to make a class and attach it to the module, while
 * handling stack push/pop to keep things from being prematurely GC'd.
 */
KrkClass * krk_createClass(KrkInstance * inModule, const char * name, KrkClass * base) {
	if (!base) base = vm.baseClasses->objectClass;
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

static KrkValue _message_getattr(int argc, KrkValue argv[], int hasKw) {
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

	krk_runtimeError(vm.exceptions->attributeError, "no attribute '%s'", AS_CSTRING(argv[1]));
	return NONE_VAL();
}
#undef DO_FIELD
#undef TO_INT_
#undef GET
#undef TO_INT
#undef WID

static KrkValue _yutani_repr(int argc, KrkValue argv[], int hasKw) {
	struct YutaniClass * self = (struct YutaniClass*)AS_INSTANCE(argv[0]);
	char out[500];
	size_t len = sprintf(out, "Yutani(fd=%d,server=%s,display_width=%d,display_height=%d)",
		fileno(self->yctx->sock),
		self->yctx->server_ident,
		(int)self->yctx->display_width,
		(int)self->yctx->display_height);
	return OBJECT_VAL(krk_copyString(out,len));
}

static KrkValue _yutani_init(int argc, KrkValue argv[], int hasKw) {
	if (yctxInstance) {
		krk_runtimeError(vm.exceptions->valueError, "class 'Yutani' is a singleton and has already been initialized.");
		return NONE_VAL();
	}

	KrkInstance * self = AS_INSTANCE(argv[0]);

	/* Connect and let's go. */
	yutani_t * yctx = yutani_init();
	if (!yctx) {
		krk_runtimeError(vm.exceptions->ioError, "Failed to connect to compositor.");
		return NONE_VAL();
	}

	init_decorations();

	((struct YutaniClass*)self)->yctx = yctx;
	yctxInstance = self;
	krk_attachNamedObject(&module->fields, "_yutani_t", (KrkObj*)self);

	return argv[0];
}

#define CHECK_YUTANI() \
	if (argc < 1 || !krk_isInstanceOf(argv[0], Yutani)) \
		return krk_runtimeError(vm.exceptions->typeError, "expected Yutani"); \
	struct YutaniClass * self = (struct YutaniClass*)AS_INSTANCE(argv[0])

static KrkValue _yutani_display_width(int argc, KrkValue argv[], int hasKw) {
	CHECK_YUTANI();
	return INTEGER_VAL(self->yctx->display_width);
}

static KrkValue _yutani_display_height(int argc, KrkValue argv[], int hasKw) {
	CHECK_YUTANI();
	return INTEGER_VAL(self->yctx->display_height);
}

static KrkValue _yutani_poll(int argc, KrkValue argv[], int hasKw) {
	CHECK_YUTANI();

	int sync = (argc > 1 && IS_BOOLEAN(argv[1])) ? AS_BOOLEAN(argv[1]) : 1;
	yutani_msg_t * result;
	if (sync) {
		result = yutani_poll(self->yctx);
	} else {
		result = yutani_poll_async(self->yctx);
	}

	if (!result) return NONE_VAL();

	KrkInstance * out = krk_newInstance(Message);
	krk_push(OBJECT_VAL(out));
	((struct MessageClass*)out)->msg = result;
	krk_attachNamedValue(&out->fields, "type", INTEGER_VAL(result->type));

	return krk_pop();
}

static KrkValue _yutani_wait_for(int argc, KrkValue argv[], int hasKw) {
	CHECK_YUTANI();
	if (argc != 2 || !IS_INTEGER(argv[1])) { krk_runtimeError(vm.exceptions->argumentError, "expected int for msgtype"); return NONE_VAL(); }
	yutani_msg_t * result = yutani_wait_for(self->yctx, AS_INTEGER(argv[1]));
	KrkInstance * out = krk_newInstance(Message);
	krk_push(OBJECT_VAL(out));
	((struct MessageClass*)out)->msg = result;
	krk_attachNamedValue(&out->fields, "type", INTEGER_VAL(result->type));

	return krk_pop();
}

static KrkValue _yutani_subscribe(int argc, KrkValue argv[], int hasKw) {
	CHECK_YUTANI();
	yutani_subscribe_windows(self->yctx);
	return NONE_VAL();
}

static KrkValue _yutani_unsubscribe(int argc, KrkValue argv[], int hasKw) {
	CHECK_YUTANI();
	yutani_unsubscribe_windows(self->yctx);
	return NONE_VAL();
}

static KrkValue _yutani_query_windows(int argc, KrkValue argv[], int hasKw) {
	CHECK_YUTANI();
	yutani_query_windows(self->yctx);
	return NONE_VAL();
}

static KrkValue _yutani_fileno(int argc, KrkValue argv[], int hasKw) {
	CHECK_YUTANI();
	return INTEGER_VAL(fileno(self->yctx->sock));
}

static KrkValue _yutani_query(int argc, KrkValue argv[], int hasKw) {
	CHECK_YUTANI();
	return INTEGER_VAL(yutani_query(self->yctx));
}

static KrkValue _yutani_menu_process_event(int argc, KrkValue argv[], int hasKw) {
	CHECK_YUTANI();
	if (argc < 2 || !krk_isInstanceOf(argv[1],Message))
		return krk_runtimeError(vm.exceptions->typeError, "expected Message");
	struct MessageClass* msg = (struct MessageClass*)AS_INSTANCE(argv[1]);

	return INTEGER_VAL(menu_process_event(self->yctx, msg->msg));
}

#define GET_ARG(p,name,type) do { \
	if (hasKw && krk_tableGet(AS_DICT(argv[argc]), OBJECT_VAL(S(#name)), &name)) { \
		if (!krk_isInstanceOf(name,type)) \
			return krk_runtimeError(vm.exceptions->typeError, #name " argument should be " #type ", not '%s'", krk_typeName(name)); \
	} else if (argc > p) { \
		name = argv[p]; \
		if (!krk_isInstanceOf(name,type)) \
			return krk_runtimeError(vm.exceptions->typeError, #name " argument should be " #type ", not '%s'", krk_typeName(name)); \
	} \
} while (0)

#define GFX_PROPERTY(name) \
static KrkValue _gfx_ ## name (int argc, KrkValue argv[], int hasKw) { \
	if (argc != 1 || !krk_isInstanceOf(argv[0], GraphicsContext)) \
		return krk_runtimeError(vm.exceptions->typeError, "Expected GraphicsContext"); \
	struct GraphicsContext * self = (struct GraphicsContext *)AS_INSTANCE(argv[0]); \
	return INTEGER_VAL(self->ctx-> name); \
}

GFX_PROPERTY(width);
GFX_PROPERTY(height);

#define CHECK_GFX() \
	if (argc < 1 || !krk_isInstanceOf(argv[0], GraphicsContext)) \
		return krk_runtimeError(vm.exceptions->typeError, "expected GraphicsContext"); \
	struct GraphicsContext * self = (struct GraphicsContext*)AS_INSTANCE(argv[0])

static KrkValue _gfx_fill(int argc, KrkValue argv[], int hasKw) {
	CHECK_GFX();
	if (argc < 2 || !krk_isInstanceOf(argv[1], YutaniColor))
		return krk_runtimeError(vm.exceptions->typeError, "fill() takes one color() argument");
	struct YutaniColor * color = (struct YutaniColor*)AS_INSTANCE(argv[1]);
	draw_fill(self->ctx, color->color);
	return NONE_VAL();
}

static KrkValue _gfx_flip(int argc, KrkValue argv[], int hasKw) {
	CHECK_GFX();
	if (self->doubleBuffered) {
		flip(self->ctx);
	}
	return NONE_VAL();
}

static KrkValue _gfx_blur(int argc, KrkValue argv[], int hasKw) {
	CHECK_GFX();
	int radius = 2;
	if (argc > 1 && IS_INTEGER(argv[1])) radius = AS_INTEGER(argv[1]);
	else if (argc > 1) return krk_runtimeError(vm.exceptions->typeError, "expected int");
	blur_context_box(self->ctx, radius);
	return NONE_VAL();
}

static KrkValue _gfx_line(int argc, KrkValue argv[], int hasKw) {
	CHECK_GFX();
	if (argc < 6 ||
		!IS_INTEGER(argv[1]) ||
		!IS_INTEGER(argv[2]) ||
		!IS_INTEGER(argv[3]) ||
		!IS_INTEGER(argv[4]) ||
		!krk_isInstanceOf(argv[5], YutaniColor)) {
		return krk_runtimeError(vm.exceptions->typeError, "line() expects 4 ints and a color");
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
			return krk_runtimeError(vm.exceptions->typeError, "thickness must be int or float, not '%s'", krk_typeName(argv[6]));
		}
	} else {
		draw_line(self->ctx,x0,x1,y0,y1,color->color);
	}

	return NONE_VAL();
}

static KrkValue _gfx_rect(int argc, KrkValue argv[], int hasKw) {
	CHECK_GFX();

	if (argc != 6 ||
		!IS_INTEGER(argv[1]) ||
		!IS_INTEGER(argv[2]) ||
		!IS_INTEGER(argv[3]) ||
		!IS_INTEGER(argv[4]) ||
		!krk_isInstanceOf(argv[5], YutaniColor)) {
		return krk_runtimeError(vm.exceptions->typeError, "rect() expects 4 ints and a color");
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
		return krk_runtimeError(vm.exceptions->typeError, "solid must be bool");
	if (!IS_NONE(radius) && !IS_INTEGER(radius))
		return krk_runtimeError(vm.exceptions->typeError, "radius must be int");
	if (!IS_NONE(radius) && AS_BOOLEAN(solid))
		return krk_runtimeError(vm.exceptions->typeError, "radius and solid can not be used together");

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
	CHECK_GFX();

	if (argc < 2 || !krk_isInstanceOf(argv[1], YutaniSprite))
		return krk_runtimeError(vm.exceptions->typeError, "expected Sprite");

	if (argc < 4 || !IS_INTEGER(argv[2]) || !IS_INTEGER(argv[3]))
		return krk_runtimeError(vm.exceptions->typeError, "expected integer coordinate pair");

	/* Potential kwargs: rotation:float, alpha:float, scale:(int,int)... */
	KrkValue rotation = NONE_VAL(), alpha = NONE_VAL(), scale=NONE_VAL(), color=NONE_VAL();
	if (hasKw) {
		krk_tableGet(AS_DICT(argv[argc]), OBJECT_VAL(S("alpha")), &alpha);
		krk_tableGet(AS_DICT(argv[argc]), OBJECT_VAL(S("rotation")), &rotation);
		krk_tableGet(AS_DICT(argv[argc]), OBJECT_VAL(S("scale")), &scale);
		krk_tableGet(AS_DICT(argv[argc]), OBJECT_VAL(S("color")), &color);
	}

	if (!IS_NONE(alpha) && !IS_FLOATING(alpha))
		return krk_runtimeError(vm.exceptions->typeError, "alpha must be float");
	if (!IS_NONE(rotation) && !IS_FLOATING(rotation))
		return krk_runtimeError(vm.exceptions->typeError, "rotation must be float");
	if (!IS_NONE(color) && !krk_isInstanceOf(color,YutaniColor))
		return krk_runtimeError(vm.exceptions->typeError, "color must be color");
	if (!IS_NONE(scale) && (!IS_TUPLE(scale) || AS_TUPLE(scale)->values.count != 2 ||
		!IS_INTEGER(AS_TUPLE(scale)->values.values[0]) ||
		!IS_INTEGER(AS_TUPLE(scale)->values.values[1])))
		return krk_runtimeError(vm.exceptions->typeError, "scale must be 2-tuple of ints");
	if (!IS_NONE(rotation) + !IS_NONE(scale) + !IS_NONE(color) > 1)
		return krk_runtimeError(vm.exceptions->typeError, "can not combine rotation / scale / color");

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

static KrkValue _sprite_repr(int argc, KrkValue argv[], int hasKw) {
	struct YutaniSprite * self = (struct YutaniSprite *)AS_INSTANCE(argv[0]);

	KrkValue file;
	krk_tableGet(&self->inst.fields, OBJECT_VAL(S("file")), &file);

	char out[500];
	size_t len = sprintf(out, "Sprite('%s',width=%d,height=%d)",
		!IS_STRING(file) ? "" : AS_CSTRING(file),
		(int)self->sprite.width,
		(int)self->sprite.height);
	return OBJECT_VAL(krk_copyString(out,len));
}

static KrkValue _sprite_init(int argc, KrkValue argv[], int hasKw) {
	if (argc < 1 || !krk_isInstanceOf(argv[0], YutaniSprite))
		return krk_runtimeError(vm.exceptions->typeError, "expected sprite");

	if (argc < 2 || !IS_STRING(argv[1]))
		return krk_runtimeError(vm.exceptions->typeError, "Sprite() takes one str argument");

	struct YutaniSprite * self = (struct YutaniSprite*)AS_INSTANCE(argv[0]);

	int result = load_sprite(&self->sprite, AS_CSTRING(argv[1]));
	if (result) {
		return krk_runtimeError(vm.exceptions->ioError, "Sprite() could not be initialized");
	}

	self->ctx = init_graphics_sprite(&self->sprite);
	krk_attachNamedValue(&self->inst.fields, "file", argv[1]);

	return argv[0];
}

#define CHECK_WINDOW() \
	if (argc < 1 || !krk_isInstanceOf(argv[0], YutaniWindow)) \
		return krk_runtimeError(vm.exceptions->typeError, "expected Window"); \
	struct WindowClass * self = (struct WindowClass*)AS_INSTANCE(argv[0]); \
	if (!self->window) return krk_runtimeError(vm.exceptions->valueError, "Window is closed")

static KrkValue _window_repr(int argc, KrkValue argv[], int hasKw) {
	CHECK_WINDOW();
	KrkValue title;
	krk_tableGet(&self->inst.fields, OBJECT_VAL(S("title")), &title);
	char out[500];
	size_t len = sprintf(out, "Window(wid=%d,title=%s,width=%d,height=%d)",
		self->window->wid,
		IS_NONE(title) ? "" : AS_CSTRING(title),
		(int)self->window->width,
		(int)self->window->height);
	return OBJECT_VAL(krk_copyString(out,len));
}

static KrkValue _window_init(int argc, KrkValue argv[], int hasKw) {
	if (!yctxInstance) return krk_runtimeError(vm.exceptions->valueError, "Compositor is not initialized");
	if (argc < 1 || !krk_isInstanceOf(argv[0], YutaniWindow))
		return krk_runtimeError(vm.exceptions->typeError, "Failed to initialize window");

	if (argc < 3 || !IS_INTEGER(argv[1]) || !IS_INTEGER(argv[2]))
		return krk_runtimeError(vm.exceptions->argumentError, "Expected at least two (integer) arguments (width, height)");

	KrkInstance * _self = AS_INSTANCE(argv[0]);
	struct WindowClass * self = (struct WindowClass*)_self;
	krk_integer_type width = AS_INTEGER(argv[1]);
	krk_integer_type height = AS_INTEGER(argv[2]);

	KrkValue flags = INTEGER_VAL(0), title = NONE_VAL(), icon = NONE_VAL(), doublebuffer = BOOLEAN_VAL(0);
	GET_ARG(3, flags, vm.baseClasses->intClass);
	GET_ARG(4, title, vm.baseClasses->strClass);
	GET_ARG(5, icon,  vm.baseClasses->strClass);
	GET_ARG(6, doublebuffer, vm.baseClasses->boolClass);

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

static KrkValue _window_flip(int argc, KrkValue argv[], int hasKw) {
	CHECK_WINDOW();
	if (self->doubleBuffered) {
		flip(self->ctx);
	}
	yutani_flip(((struct YutaniClass*)yctxInstance)->yctx, self->window);
	return NONE_VAL();
}

static KrkValue _window_move(int argc, KrkValue argv[], int hasKw) {
	CHECK_WINDOW();
	if (argc < 3 || !IS_INTEGER(argv[1]) || !IS_INTEGER(argv[2]))
		return krk_runtimeError(vm.exceptions->typeError, "expected two integer arguments");
	yutani_window_move(((struct YutaniClass*)yctxInstance)->yctx, self->window, AS_INTEGER(argv[1]), AS_INTEGER(argv[2]));
	return NONE_VAL();
}

static KrkValue _window_set_focused(int argc, KrkValue argv[], int hasKw) {
	CHECK_WINDOW();
	if (argc < 2 || !IS_INTEGER(argv[1]))
		return krk_runtimeError(vm.exceptions->typeError, "expected integer argument");
	self->window->focused = AS_INTEGER(argv[1]);
	return NONE_VAL();
}

static KrkValue _window_close(int argc, KrkValue argv[], int hasKw) {
	CHECK_WINDOW();
	yutani_close(((struct YutaniClass*)yctxInstance)->yctx, self->window);
	self->window = NULL;
	release_graphics_yutani(self->ctx);
	self->ctx = NULL;
	return NONE_VAL();
}

static KrkValue _window_set_stack(int argc, KrkValue argv[], int hasKw) {
	CHECK_WINDOW();
	if (argc < 2 || !IS_INTEGER(argv[1])) return krk_runtimeError(vm.exceptions->typeError, "expected int for z-order");
	int z = AS_INTEGER(argv[1]);
	yutani_set_stack(((struct YutaniClass*)yctxInstance)->yctx, self->window, z);
	return NONE_VAL();
}

static KrkValue _window_update_shape(int argc, KrkValue argv[], int hasKw) {
	CHECK_WINDOW();
	if (argc < 2 || !IS_INTEGER(argv[1])) return krk_runtimeError(vm.exceptions->typeError, "expected int for shape specifier");
	int set_shape = AS_INTEGER(argv[1]);
	yutani_window_update_shape(((struct YutaniClass*)yctxInstance)->yctx, self->window, set_shape);
	return NONE_VAL();
}

static KrkValue _window_warp_mouse(int argc, KrkValue argv[], int hasKw) {
	CHECK_WINDOW();
	if (argc < 3 || !IS_INTEGER(argv[1]) || !IS_INTEGER(argv[2]))
		return krk_runtimeError(vm.exceptions->typeError, "expected two int values for x, y");
	int32_t x = AS_INTEGER(argv[1]);
	int32_t y = AS_INTEGER(argv[2]);
	yutani_window_warp_mouse(((struct YutaniClass*)yctxInstance)->yctx, self->window, x, y);
	return NONE_VAL();
}

static KrkValue _window_show_mouse(int argc, KrkValue argv[], int hasKw) {
	CHECK_WINDOW();
	if (argc < 2 || !IS_INTEGER(argv[1])) return krk_runtimeError(vm.exceptions->typeError, "expected int for show_mouse");
	int show_mouse = AS_INTEGER(argv[1]);
	yutani_window_show_mouse(((struct YutaniClass*)yctxInstance)->yctx, self->window, show_mouse);
	return NONE_VAL();
}

static KrkValue _window_resize_start(int argc, KrkValue argv[], int hasKw) {
	CHECK_WINDOW();
	if (argc < 2 || !IS_INTEGER(argv[1])) return krk_runtimeError(vm.exceptions->typeError, "expected int for direction");
	yutani_scale_direction_t direction = AS_INTEGER(argv[1]);
	yutani_window_resize_start(((struct YutaniClass*)yctxInstance)->yctx, self->window, direction);
	return NONE_VAL();
}

static KrkValue _window_resize(int argc, KrkValue argv[], int hasKw) {
	CHECK_WINDOW();
	if (argc < 3 || !IS_INTEGER(argv[1]) || !IS_INTEGER(argv[2]))
		return krk_runtimeError(vm.exceptions->typeError, "expected two int values for width, height");
	uint32_t width = AS_INTEGER(argv[1]);
	uint32_t height = AS_INTEGER(argv[2]);
	yutani_window_resize(((struct YutaniClass*)yctxInstance)->yctx, self->window, width, height);
	return NONE_VAL();
}

static KrkValue _window_resize_offer(int argc, KrkValue argv[], int hasKw) {
	CHECK_WINDOW();
	if (argc < 3 || !IS_INTEGER(argv[1]) || !IS_INTEGER(argv[2]))
		return krk_runtimeError(vm.exceptions->typeError, "expected two int values for width, height");
	uint32_t width = AS_INTEGER(argv[1]);
	uint32_t height = AS_INTEGER(argv[2]);
	yutani_window_resize_offer(((struct YutaniClass*)yctxInstance)->yctx, self->window, width, height);
	return NONE_VAL();
}

static KrkValue _window_resize_accept(int argc, KrkValue argv[], int hasKw) {
	CHECK_WINDOW();
	if (argc < 3 || !IS_INTEGER(argv[1]) || !IS_INTEGER(argv[2]))
		return krk_runtimeError(vm.exceptions->typeError, "expected two int values for width, height");
	uint32_t width = AS_INTEGER(argv[1]);
	uint32_t height = AS_INTEGER(argv[2]);
	yutani_window_resize_accept(((struct YutaniClass*)yctxInstance)->yctx, self->window, width, height);
	return NONE_VAL();
}

static KrkValue _window_resize_done(int argc, KrkValue argv[], int hasKw) {
	CHECK_WINDOW();
	yutani_window_resize_done(((struct YutaniClass*)yctxInstance)->yctx, self->window);
	return NONE_VAL();
}

static KrkValue _window_advertise(int argc, KrkValue argv[], int hasKw) {
	CHECK_WINDOW();
	if (argc < 2 || !IS_STRING(argv[1]))
		return krk_runtimeError(vm.exceptions->typeError, "expected string for title");
	if (argc > 2 && !IS_STRING(argv[2]))
		return krk_runtimeError(vm.exceptions->typeError, "expected string for icon");

	if (argc > 2) {
		yutani_window_advertise_icon(((struct YutaniClass*)yctxInstance)->yctx, self->window, AS_CSTRING(argv[1]), AS_CSTRING(argv[2]));
	} else {
		yutani_window_advertise(((struct YutaniClass*)yctxInstance)->yctx, self->window, AS_CSTRING(argv[1]));
	}
	return NONE_VAL();
}

static KrkValue _window_special_request(int argc, KrkValue argv[], int hasKw) {
	CHECK_WINDOW();
	if (argc < 2 || !IS_INTEGER(argv[1])) return krk_runtimeError(vm.exceptions->typeError, "expected int for request");
	uint32_t request = AS_INTEGER(argv[1]);
	yutani_special_request(((struct YutaniClass*)yctxInstance)->yctx, self->window, request);
	return NONE_VAL();
}

static KrkValue _window_reinit(int argc, KrkValue argv[], int hasKw) {
	CHECK_WINDOW();
	reinit_graphics_yutani(self->ctx, self->window);
	return NONE_VAL();
}

#define WINDOW_PROPERTY(name) \
static KrkValue _window_ ## name (int argc, KrkValue argv[], int hasKw) { \
	if (argc != 1 || !krk_isInstanceOf(argv[0], YutaniWindow)) \
		return krk_runtimeError(vm.exceptions->typeError, "Expected Window"); \
	struct WindowClass * self = (struct WindowClass*)AS_INSTANCE(argv[0]); \
	return INTEGER_VAL(self->window-> name); \
}

WINDOW_PROPERTY(wid);
WINDOW_PROPERTY(x);
WINDOW_PROPERTY(y);
WINDOW_PROPERTY(focused);

static KrkValue _decor_get_bounds(int argc, KrkValue argv[], int hasKw) {
	if (argc > 0 && !krk_isInstanceOf(argv[0], YutaniWindow))
		return krk_runtimeError(vm.exceptions->typeError, "expected window");
	struct decor_bounds bounds;

	decor_get_bounds((argc > 0) ? ((struct WindowClass*)AS_INSTANCE(argv[0]))->window : NULL,
		&bounds);

	KrkValue result = krk_dict_of(0, NULL, 0);
	krk_push(result);

#define SET(val) krk_attachNamedValue(AS_DICT(result), #val, INTEGER_VAL(bounds. val));

	SET(top_height);
	SET(bottom_height);
	SET(left_width);
	SET(right_width);
	SET(width);
	SET(height);

	return krk_pop();
}

static KrkValue _decor_handle_event(int argc, KrkValue argv[], int hasKw) {
	if (argc < 1 || !krk_isInstanceOf(argv[0], Message))
		return krk_runtimeError(vm.exceptions->typeError, "expected message");
	return INTEGER_VAL(decor_handle_event(((struct YutaniClass*)yctxInstance)->yctx, ((struct MessageClass*)AS_INSTANCE(argv[0]))->msg));
}

static KrkValue _decor_render(int argc, KrkValue argv[], int hasKw) {
	if (argc < 1 || !krk_isInstanceOf(argv[0], YutaniWindow))
		return krk_runtimeError(vm.exceptions->typeError, "expected window");
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

static KrkValue _decor_show_default_menu(int argc, KrkValue argv[], int hasKw) {
	if (argc < 1 || !krk_isInstanceOf(argv[0], YutaniWindow))
		return krk_runtimeError(vm.exceptions->typeError, "show_default_menu() expects Window");
	if (argc < 3 || !IS_INTEGER(argv[1]) || !IS_INTEGER(argv[2]))
		return krk_runtimeError(vm.exceptions->typeError, "show_default_menu() expects int coordinate pair");

	struct WindowClass * window = (void*)AS_INSTANCE(argv[0]);
	int32_t x = AS_INTEGER(argv[1]);
	int32_t y = AS_INTEGER(argv[2]);

	decor_show_default_menu(window->window, x, y);
	return NONE_VAL();
}

static KrkValue _yutani_color_init(int argc, KrkValue argv[], int hasKw) {
	if (argc < 4 || !IS_INTEGER(argv[1]) || !IS_INTEGER(argv[2]) || !IS_INTEGER(argv[3]) ||
	   (argc > 5) || (argc == 5 && !IS_INTEGER(argv[4]))) return krk_runtimeError(vm.exceptions->typeError, "color() expects three or four integer arguments");
	if (!krk_isInstanceOf(argv[0], YutaniColor)) return krk_runtimeError(vm.exceptions->typeError, "expected color [__init__], not '%s'", krk_typeName(argv[0]));
	struct YutaniColor * self = (struct YutaniColor*)AS_INSTANCE(argv[0]);
	if (argc == 5) {
		self->color = rgba(AS_INTEGER(argv[1]),AS_INTEGER(argv[2]),AS_INTEGER(argv[3]),AS_INTEGER(argv[4]));
	} else {
		self->color = rgb(AS_INTEGER(argv[1]),AS_INTEGER(argv[2]),AS_INTEGER(argv[3]));
	}
	return argv[0];
}

static KrkValue _yutani_color_repr(int argc, KrkValue argv[], int hasKw) {
	if (argc != 1 || !krk_isInstanceOf(argv[0], YutaniColor)) return krk_runtimeError(vm.exceptions->typeError, "expected color [__repr__], not '%s'", krk_typeName(argv[0]));
	struct YutaniColor * self = (struct YutaniColor*)AS_INSTANCE(argv[0]);
	char tmp[30];
	if (_ALP(self->color) != 255) {
		sprintf(tmp, "color<#%02x%02x%02x%02x>", (int)_RED(self->color), (int)_GRE(self->color), (int)_BLU(self->color), (int)_ALP(self->color));
	} else {
		sprintf(tmp, "color<#%02x%02x%02x>", (int)_RED(self->color), (int)_GRE(self->color), (int)_BLU(self->color));
	}
	return OBJECT_VAL(krk_copyString(tmp,strlen(tmp)));
}

static KrkValue _yutani_color_str(int argc, KrkValue argv[], int hasKw) {
	if (argc != 1 || !krk_isInstanceOf(argv[0], YutaniColor)) return krk_runtimeError(vm.exceptions->typeError, "expected color [__str__], not '%s'", krk_typeName(argv[0]));
	struct YutaniColor * self = (struct YutaniColor*)AS_INSTANCE(argv[0]);
	char tmp[30];
	if (_ALP(self->color) != 255) {
		sprintf(tmp, "#%02x%02x%02x%02x", (int)_RED(self->color), (int)_GRE(self->color), (int)_BLU(self->color), (int)_ALP(self->color));
	} else {
		sprintf(tmp, "#%02x%02x%02x", (int)_RED(self->color), (int)_GRE(self->color), (int)_BLU(self->color));
	}
	return OBJECT_VAL(krk_copyString(tmp,strlen(tmp)));
}

#define CHECK_FONT() \
	if (argc < 1 || !krk_isInstanceOf(argv[0], YutaniFont)) \
		return krk_runtimeError(vm.exceptions->typeError, "expected Font"); \
	struct YutaniFont * self = (struct YutaniFont*)AS_INSTANCE(argv[0])

static KrkValue _font_init(int argc, KrkValue argv[], int hasKw) {
	CHECK_FONT();

	if (argc < 2 || !IS_INTEGER(argv[1]))
		return krk_runtimeError(vm.exceptions->typeError, "expected int for font type");
	if (argc < 3 || !IS_INTEGER(argv[2]))
		return krk_runtimeError(vm.exceptions->typeError, "expected int for font size");

	KrkValue fontGamma = FLOATING_VAL(1.7);
	KrkValue fontStroke = FLOATING_VAL(0.75);
	KrkValue fontColor = NONE_VAL();
	if (hasKw) {
		krk_tableGet(AS_DICT(argv[argc]), OBJECT_VAL(S("gamma")), &fontGamma);
		krk_tableGet(AS_DICT(argv[argc]), OBJECT_VAL(S("stroke")), &fontStroke);
		krk_tableGet(AS_DICT(argv[argc]), OBJECT_VAL(S("color")), &fontColor);
		if (!IS_FLOATING(fontGamma)) return krk_runtimeError(vm.exceptions->typeError, "expected float for gamma");
		if (!IS_FLOATING(fontStroke)) return krk_runtimeError(vm.exceptions->typeError, "expected float for stroke");
		if (!krk_isInstanceOf(fontColor, YutaniColor)) return krk_runtimeError(vm.exceptions->typeError, "expected color");
	}

	self->fontType = AS_INTEGER(argv[1]);
	self->fontSize = AS_INTEGER(argv[2]);
	self->fontGamma = AS_FLOATING(fontGamma);
	self->fontStroke = AS_FLOATING(fontStroke);
	self->fontColor = IS_NONE(fontColor) ? rgb(0,0,0) : ((struct YutaniColor*)AS_INSTANCE(fontColor))->color;

	return argv[0];
}

static KrkValue _font_size(int argc, KrkValue argv[], int hasKw) {
	CHECK_FONT();
	return INTEGER_VAL(self->fontSize);
}

static KrkValue _font_draw_string(int argc, KrkValue argv[], int hasKw) {
	CHECK_FONT();
	if (argc < 2 || !krk_isInstanceOf(argv[1], GraphicsContext))
		return krk_runtimeError(vm.exceptions->typeError, "expected GraphicsContext");
	if (argc < 3 || !IS_STRING(argv[2]))
		return krk_runtimeError(vm.exceptions->typeError, "expected str");
	if (argc < 5 || !IS_INTEGER(argv[3]) || !IS_INTEGER(argv[4]))
		return krk_runtimeError(vm.exceptions->typeError, "expected int coordinate pair");

	gfx_context_t * ctx = ((struct GraphicsContext*)AS_INSTANCE(argv[1]))->ctx;
	const char * str = AS_CSTRING(argv[2]);
	int32_t x = AS_INTEGER(argv[3]);
	int32_t y = AS_INTEGER(argv[4]);

	return INTEGER_VAL(draw_sdf_string_stroke(ctx,x,y,str,self->fontSize,self->fontColor,self->fontType,self->fontGamma,self->fontStroke));
}

static KrkValue _font_width(int argc, KrkValue argv[], int hasKw) {
	CHECK_FONT();
	if (argc < 2 || !IS_STRING(argv[1]))
		return krk_runtimeError(vm.exceptions->typeError, "expected str");

	const char * str = AS_CSTRING(argv[1]);
	return INTEGER_VAL(draw_sdf_string_width(str, self->fontSize, self->fontType));
}

static void _MenuBar_gcsweep(KrkInstance * _self) {
	struct MenuBarClass * self = (struct MenuBarClass*)_self;
	if (self->menuBar.entries) {
		for (size_t i = 0; self->menuBar.entries[i].title; ++i) {
			free(self->menuBar.entries[i].title);
			free(self->menuBar.entries[i].action);
		}
		free(self->menuBar.entries);
	}
}

static void _menubar_callback(struct menu_bar * _self) {
	struct MenuBarClass * self = _self->_private;
	KrkValue callback;
	if (krk_tableGet(&self->inst.fields, OBJECT_VAL(S("callback")), &callback)) {
		krk_push(OBJECT_VAL(self));
		krk_callSimple(callback, 1, 0);
	}
}

static KrkValue _MenuBar_init(int argc, KrkValue argv[], int hasKw) {
	if (argc < 1 || !krk_isInstanceOf(argv[0], MenuBarClass))
		return krk_runtimeError(vm.exceptions->typeError, "expected MenuBar");
	if (argc < 2 || !IS_TUPLE(argv[1]))
		return krk_runtimeError(vm.exceptions->typeError, "expected tuple of tuples");

	struct MenuBarClass * self = (struct MenuBarClass*)AS_INSTANCE(argv[0]);
	self->menuBar.entries = malloc(sizeof(struct menu_bar_entries) * (AS_TUPLE(argv[1])->values.count + 1));
	for (size_t i = 0; i < AS_TUPLE(argv[1])->values.count; ++i) {
		if (!IS_TUPLE(AS_TUPLE(argv[1])->values.values[i]) ||
			AS_TUPLE(AS_TUPLE(argv[1])->values.values[i])->values.count != 2 ||
			!IS_STRING(AS_TUPLE(AS_TUPLE(argv[1])->values.values[i])->values.values[0]) || 
			!IS_STRING(AS_TUPLE(AS_TUPLE(argv[1])->values.values[i])->values.values[1])) {
			return krk_runtimeError(vm.exceptions->typeError, "invalid menu bar entry: expected (str,str) but %d is '%s'",
				(int)i, krk_typeName(AS_TUPLE(argv[1])->values.values[i]));
		}

		KrkString * title = AS_STRING(AS_TUPLE(AS_TUPLE(argv[1])->values.values[i])->values.values[0]);
		KrkString * action = AS_STRING(AS_TUPLE(AS_TUPLE(argv[1])->values.values[i])->values.values[1]);

		self->menuBar.entries[i].title = strdup(title->chars);
		self->menuBar.entries[i].action = strdup(action->chars);
	}
	self->menuBar.entries[AS_TUPLE(argv[1])->values.count].title = NULL;
	self->menuBar.entries[AS_TUPLE(argv[1])->values.count].action = NULL;

	self->menuBar.set = menu_set_create();
	self->menuBar._private = self;
	self->menuBar.redraw_callback = _menubar_callback;

	krk_attachNamedValue(&self->inst.fields, "entries", argv[1]);

	/* Give ourselves a dict to track the same information */
	KrkValue dict = krk_dict_of(0,NULL, 0);
	krk_attachNamedValue(&self->inst.fields, "set", dict);

	return argv[0];
}

static KrkValue _MenuBar_place(int argc, KrkValue argv[], int hasKw) {
	if (argc < 1 || !krk_isInstanceOf(argv[0], MenuBarClass))
		return krk_runtimeError(vm.exceptions->typeError, "expected MenuBar");
	struct MenuBarClass * self = (struct MenuBarClass*)AS_INSTANCE(argv[0]);
	if (argc < 4 || !IS_INTEGER(argv[1]) || !IS_INTEGER(argv[2]) || !IS_INTEGER(argv[3]))
		return krk_runtimeError(vm.exceptions->typeError, "expected int for x, y, width");
	if (argc < 5 || !krk_isInstanceOf(argv[4],YutaniWindow))
		return krk_runtimeError(vm.exceptions->typeError, "expected Window");

	self->menuBar.x = AS_INTEGER(argv[1]);
	self->menuBar.y = AS_INTEGER(argv[2]);
	self->menuBar.width = AS_INTEGER(argv[3]);
	self->menuBar.window = ((struct WindowClass*)AS_INSTANCE(argv[4]))->window;
	return NONE_VAL();
}

static KrkValue _MenuBar_render(int argc, KrkValue argv[], int hasKw) {
	if (argc < 1 || !krk_isInstanceOf(argv[0], MenuBarClass))
		return krk_runtimeError(vm.exceptions->typeError, "expected MenuBar");
	struct MenuBarClass * self = (struct MenuBarClass*)AS_INSTANCE(argv[0]);
	if (argc < 2 || !krk_isInstanceOf(argv[1], GraphicsContext))
		return krk_runtimeError(vm.exceptions->typeError, "expected GraphicsContext");
	menu_bar_render(&self->menuBar, ((struct GraphicsContext*)AS_INSTANCE(argv[1]))->ctx);
	return NONE_VAL();
}

static KrkValue _MenuBar_mouse_event(int argc, KrkValue argv[], int hasKw) {
	if (argc < 1 || !krk_isInstanceOf(argv[0], MenuBarClass))
		return krk_runtimeError(vm.exceptions->typeError, "expected MenuBar");
	struct MenuBarClass * self = (struct MenuBarClass*)AS_INSTANCE(argv[0]);
	if (argc < 3 || !krk_isInstanceOf(argv[1], YutaniWindow) ||
		!krk_isInstanceOf(argv[2], Message))
		return krk_runtimeError(vm.exceptions->typeError, "expected Window and Message");

	struct MessageClass * msg = ((struct MessageClass*)AS_INSTANCE(argv[2]));
	struct yutani_msg_window_mouse_event * me = (struct yutani_msg_window_mouse_event*)msg->msg->data;

	return INTEGER_VAL(menu_bar_mouse_event(((struct YutaniClass*)yctxInstance)->yctx,
		((struct WindowClass*)AS_INSTANCE(argv[1]))->window,
		&self->menuBar, me, me->new_x, me->new_y));
}

static KrkValue _MenuBar_insert(int argc, KrkValue argv[], int hasKw) {
	if (argc < 1 || !krk_isInstanceOf(argv[0], MenuBarClass))
		return krk_runtimeError(vm.exceptions->typeError, "expected MenuBar");
	struct MenuBarClass * self = (struct MenuBarClass*)AS_INSTANCE(argv[0]);
	if (argc < 3 || !IS_STRING(argv[1]) || !krk_isInstanceOf(argv[2], MenuListClass))
		return krk_runtimeError(vm.exceptions->typeError, "expected str and MenuList");

	menu_set_insert(self->menuBar.set, AS_CSTRING(argv[1]), ((struct MenuListClass*)AS_INSTANCE(argv[2]))->menuList);

	/* Also assign it to our dict */
	KrkValue dict = NONE_VAL();
	krk_tableGet(&self->inst.fields, OBJECT_VAL(S("set")), &dict);
	if (IS_NONE(dict) || !krk_isInstanceOf(dict,vm.baseClasses->dictClass))
		return krk_runtimeError(vm.exceptions->baseException, "Failed to get set entries?");
	krk_tableSet(AS_DICT(dict), argv[1], argv[2]);

	return NONE_VAL();
}

static KrkValue _MenuList_init(int argc, KrkValue argv[], int hasKw) {
	if (argc < 1 || !krk_isInstanceOf(argv[0], MenuListClass))
		return krk_runtimeError(vm.exceptions->typeError, "expected MenuList");
	struct MenuListClass * self = (struct MenuListClass*)AS_INSTANCE(argv[0]);
	self->menuList = menu_create();

	/* Give us a list to put entries in for GC tracking and retrieval by kuroko code */
	KrkValue list = krk_list_of(0,NULL,0);
	krk_attachNamedValue(&self->inst.fields, "entries", list);

	return argv[0];
}

static KrkValue _MenuList_insert(int argc, KrkValue argv[], int hasKw) {
	if (argc < 1 || !krk_isInstanceOf(argv[0], MenuListClass))
		return krk_runtimeError(vm.exceptions->typeError, "expected MenuList");
	struct MenuListClass * self = (struct MenuListClass*)AS_INSTANCE(argv[0]);
	if (argc < 2 || !krk_isInstanceOf(argv[1], MenuEntryClass))
		return krk_runtimeError(vm.exceptions->typeError, "Expected MenuEntry");

	/* Append to menu */
	menu_insert(self->menuList, ((struct MenuEntryClass*)AS_INSTANCE(argv[1]))->menuEntry);

	/* Append to internal list */
	KrkValue list = NONE_VAL();
	krk_tableGet(&self->inst.fields, OBJECT_VAL(S("entries")), &list);
	if (IS_NONE(list) || !krk_isInstanceOf(list,vm.baseClasses->listClass))
		return krk_runtimeError(vm.exceptions->baseException, "Failed to get entries?");
	krk_writeValueArray(AS_LIST(list), argv[1]);

	return NONE_VAL();
}

static void _MenuEntry_callback_internal(struct MenuEntry * _self) {
	struct MenuEntryClass * self = (struct MenuEntryClass *)_self->_private;
	KrkValue callback = NONE_VAL();
	krk_tableGet(&self->inst.fields, OBJECT_VAL(S("callback")), &callback);
	krk_push(OBJECT_VAL(self));
	krk_callSimple(callback, 1, 0);
}

static KrkValue _MenuEntry_init(int argc, KrkValue argv[], int hasKw) {
	if (argc < 1 || !krk_isInstanceOf(argv[0], MenuEntryClass))
		return krk_runtimeError(vm.exceptions->typeError, "expected MenuEntry");
	struct MenuEntryClass * self = (struct MenuEntryClass*)AS_INSTANCE(argv[0]);

	if (argc < 3 || !IS_STRING(argv[1]))
		return krk_runtimeError(vm.exceptions->typeError, "expected title and callback");

	KrkValue icon = NONE_VAL(), action = NONE_VAL();
	if (hasKw) {
		krk_tableGet(AS_DICT(argv[argc]), OBJECT_VAL(S("icon")), &icon);
		krk_tableGet(AS_DICT(argv[argc]), OBJECT_VAL(S("action")), &action);
		if (!IS_NONE(icon) && !IS_STRING(icon))
			return krk_runtimeError(vm.exceptions->typeError, "icon must be str, not '%s'", krk_typeName(icon));
		if (!IS_NONE(action) && !IS_STRING(action))
			return krk_runtimeError(vm.exceptions->typeError, "action must be str, not '%s'", krk_typeName(action));
	}

	self->menuEntry = menu_create_normal(
		IS_STRING(icon) ? AS_CSTRING(icon) : NULL,
		IS_STRING(action) ? AS_CSTRING(action) : NULL,
		AS_CSTRING(argv[1]),
		_MenuEntry_callback_internal);

	self->menuEntry->_private = self;

	krk_attachNamedValue(&self->inst.fields, "callback", argv[2]);

	return argv[0];
}

/* TODO properties: icon, action, title */

static KrkValue _MenuEntrySubmenu_init(int argc, KrkValue argv[], int hasKw) {
	if (argc < 1 || !krk_isInstanceOf(argv[0], MenuEntrySubmenuClass))
		return krk_runtimeError(vm.exceptions->typeError, "expected MenuEntrySubmenu");

	struct MenuEntryClass * self = (struct MenuEntryClass*)AS_INSTANCE(argv[0]);

	if (argc < 2 || !IS_STRING(argv[1]))
		return krk_runtimeError(vm.exceptions->typeError, "expected title to be a str");

	KrkValue icon = NONE_VAL(), action = NONE_VAL();
	if (hasKw) {
		krk_tableGet(AS_DICT(argv[argc]), OBJECT_VAL(S("icon")), &icon);
		krk_tableGet(AS_DICT(argv[argc]), OBJECT_VAL(S("action")), &action);
		if (!IS_NONE(icon) && !IS_STRING(icon))
			return krk_runtimeError(vm.exceptions->typeError, "icon must be str, not '%s'", krk_typeName(icon));
		if (!IS_NONE(action) && !IS_STRING(action))
			return krk_runtimeError(vm.exceptions->typeError, "action must be str, not '%s'", krk_typeName(action));
	}

	self->menuEntry = menu_create_submenu(
		IS_STRING(icon) ? AS_CSTRING(icon) : NULL,
		IS_STRING(action) ? AS_CSTRING(action) : NULL,
		AS_CSTRING(argv[1]));

	self->menuEntry->_private = self;

	return argv[0];
}

static KrkValue _MenuEntrySeparator_init(int argc, KrkValue argv[], int hasKw) {
	if (argc < 1 || !krk_isInstanceOf(argv[0], MenuEntrySeparatorClass))
		return krk_runtimeError(vm.exceptions->typeError, "expected MenuEntrySeparator");
	struct MenuEntryClass * self = (struct MenuEntryClass*)AS_INSTANCE(argv[0]);
	self->menuEntry = menu_create_separator();
	self->menuEntry->_private = self;
	return argv[0];
}

KrkValue krk_module_onload__yutani(void) {
	module = krk_newInstance(vm.baseClasses->moduleClass);
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
#define TYPE(type) krk_attachNamedValue(&Message->methods, "MSG_" #type, INTEGER_VAL(YUTANI_MSG_ ## type))
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
	krk_defineNative(&Message->methods, "__getattr__", _message_getattr);
	krk_finalizeClass(Message);

	/**
	 * class color():
	 *     rgb(a) value for use with graphics functions.
	 */
	YutaniColor = krk_createClass(module, "color", NULL);
	YutaniColor->allocSize = sizeof(struct YutaniColor);
	YutaniColor->docstring = S("color(r,g,b,a=255)\n  Representation of an RGB(A) color.");
	krk_defineNative(&YutaniColor->methods, "__init__", _yutani_color_init);
	krk_defineNative(&YutaniColor->methods, "__repr__", _yutani_color_repr);
	krk_defineNative(&YutaniColor->methods, "__str__", _yutani_color_str);
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
	Yutani->docstring = S("Yutani()\n  Establish a connection to the compositor display server.");
	krk_defineNative(&Yutani->methods, "display_width", _yutani_display_width)->flags |= KRK_NATIVE_FLAGS_IS_DYNAMIC_PROPERTY;
	krk_defineNative(&Yutani->methods, "display_height", _yutani_display_height)->flags |= KRK_NATIVE_FLAGS_IS_DYNAMIC_PROPERTY;
	krk_defineNative(&Yutani->methods, "__repr__", _yutani_repr);
	krk_defineNative(&Yutani->methods, "__init__", _yutani_init);
	krk_defineNative(&Yutani->methods, "poll", _yutani_poll);
	krk_defineNative(&Yutani->methods, "wait_for", _yutani_wait_for);
	krk_defineNative(&Yutani->methods, "subscribe", _yutani_subscribe);
	krk_defineNative(&Yutani->methods, "unsubscribe", _yutani_unsubscribe);
	krk_defineNative(&Yutani->methods, "query_windows", _yutani_query_windows);
	krk_defineNative(&Yutani->methods, "fileno", _yutani_fileno);
	krk_defineNative(&Yutani->methods, "query", _yutani_query);
	krk_defineNative(&Yutani->methods, "menu_process_event", _yutani_menu_process_event);
	#if 0
	krk_defineNative(&Yutani->methods, "focus_window", _yutani_focus_window);
	krk_defineNative(&Yutani->methods, "session_end", _yutani_session_end);
	krk_defineNative(&Yutani->methods, "key_bind", _yutani_key_bind);
	#endif
	krk_finalizeClass(Yutani);

	/**
	 * class GraphicsContext():
	 *     ctx = gfx_context_t *
	 */
	GraphicsContext = krk_createClass(module, "GraphicsContext", NULL);
	GraphicsContext->allocSize = sizeof(struct GraphicsContext);
	krk_defineNative(&GraphicsContext->methods, "width", _gfx_width)->flags |= KRK_NATIVE_FLAGS_IS_DYNAMIC_PROPERTY;
	krk_defineNative(&GraphicsContext->methods, "height", _gfx_height)->flags |= KRK_NATIVE_FLAGS_IS_DYNAMIC_PROPERTY;
	krk_defineNative(&GraphicsContext->methods, "fill", _gfx_fill)->doc =
		"GraphicsContext.fill(color)\n"
		"  Fill the entire context with the given color.";
	krk_defineNative(&GraphicsContext->methods, "flip", _gfx_flip)->doc =
		"GraphicsContext.flip()\n"
		"  If the context is double-buffered, flip its backbuffer.";
	krk_defineNative(&GraphicsContext->methods, "blur", _gfx_blur)->doc =
		"GraphicsContext.blur(radius=2)\n"
		"  Perform an in-place box blur on this graphics context.";
	krk_defineNative(&GraphicsContext->methods, "line", _gfx_line)->doc =
		"GraphicsContext.line(x0,x1,y0,y1,color,thickness=None)\n"
		"  Draw a line between the given points. If thickness is not provided, uses a\n"
		"  a simple Bresenham algorithm. If thickness is an int, draws with a box-shaped pen.\n"
		"  If thickness is a float, draws using a point-distance antialiasing algorithm.";
	krk_defineNative(&GraphicsContext->methods, "rect", _gfx_rect)->doc =
		"GraphicsContext.rect(x,y,width,height,color,solid=False,radius=None)\n"
		"  Draw a filled rectangle. If solid is True, paints the given color directly to\n"
		"  the underlying backbuffer with no alpha calculations. If radius is provided,\n"
		"  draws a rounded rectangle.";
	krk_defineNative(&GraphicsContext->methods, "draw_sprite", _gfx_draw_sprite)->doc =
		"GraphicsContext.draw_sprite(sprite,x,y,alpha=None,rotation=None,scale=None,color=None)\n"
		"  Blit a sprite to this graphics context at the given coordinates.\n"
		"  alpha:    float of opacity; 1.0 = fully opaque (default)\n"
		"  rotation: float of radians; when a rotation is given, the coordinates provided are\n"
		"            the center of the rendered sprite, rather than the upper left corner.\n"
		"  scale:    (int,int) of final resolution of sprite; can not be used with rotation.\n"
		"  color:    color to paint the sprite as, can not be used with rotation or scale;\n"
		"            used to paint a given color with this sprite as a 'brush'. Useful for\n"
		"            colored icons, such as those found in the panel.";
	krk_finalizeClass(GraphicsContext);

	/**
	 * class Window(GraphicsContext):
	 *     ctx = gfx_context_t *
	 *     window = yutani_window_t *
	 */
	YutaniWindow = krk_createClass(module, "Window", GraphicsContext);
	YutaniWindow->allocSize = sizeof(struct WindowClass);
	YutaniWindow->docstring = S("Window(width,height,flags=0,title=None,icon=None,doublebuffer=False)\n"
		"  Create a new window and initializes a graphics rendering context for it.");
	krk_defineNative(&YutaniWindow->methods, "__repr__", _window_repr);
	krk_defineNative(&YutaniWindow->methods, "__init__", _window_init);
	krk_defineNative(&YutaniWindow->methods, "flip", _window_flip);
	krk_defineNative(&YutaniWindow->methods, "move", _window_move);
	krk_defineNative(&YutaniWindow->methods, "set_focused", _window_set_focused);
	krk_defineNative(&YutaniWindow->methods, "close", _window_close);
	krk_defineNative(&YutaniWindow->methods, "set_stack", _window_set_stack);
	krk_defineNative(&YutaniWindow->methods, "special_request", _window_special_request);
	krk_defineNative(&YutaniWindow->methods, "resize", _window_resize);
	krk_defineNative(&YutaniWindow->methods, "resize_start", _window_resize_start);
	krk_defineNative(&YutaniWindow->methods, "resize_done", _window_resize_done);
	krk_defineNative(&YutaniWindow->methods, "resize_offer", _window_resize_offer);
	krk_defineNative(&YutaniWindow->methods, "resize_accept", _window_resize_accept);
	krk_defineNative(&YutaniWindow->methods, "update_shape", _window_update_shape);
	krk_defineNative(&YutaniWindow->methods, "show_mouse", _window_show_mouse);
	krk_defineNative(&YutaniWindow->methods, "warp_mouse", _window_warp_mouse);
	krk_defineNative(&YutaniWindow->methods, "set_stack", _window_set_stack);
	krk_defineNative(&YutaniWindow->methods, "advertise", _window_advertise);
	krk_defineNative(&YutaniWindow->methods, "reinit", _window_reinit);

	krk_defineNative(&YutaniWindow->methods, "wid", _window_wid)->flags |= KRK_NATIVE_FLAGS_IS_DYNAMIC_PROPERTY;
	krk_defineNative(&YutaniWindow->methods, "x", _window_x)->flags |= KRK_NATIVE_FLAGS_IS_DYNAMIC_PROPERTY;
	krk_defineNative(&YutaniWindow->methods, "y", _window_y)->flags |= KRK_NATIVE_FLAGS_IS_DYNAMIC_PROPERTY;
	krk_defineNative(&YutaniWindow->methods, "focused", _window_focused)->flags |= KRK_NATIVE_FLAGS_IS_DYNAMIC_PROPERTY;
	krk_finalizeClass(YutaniWindow);

	/**
	 * class Sprite(GraphicsContext):
	 *     ctx = gfx_context_t *
	 *     sprite = sprite_t
	 */
	YutaniSprite = krk_createClass(module, "Sprite", GraphicsContext);
	YutaniSprite->allocSize = sizeof(struct YutaniSprite);
	YutaniSprite->_ongcsweep = _sprite_sweep;
	YutaniSprite->docstring = S("Sprite(filename)\n  Create a sprite from the requested texture file.");
	krk_defineNative(&YutaniSprite->methods, "__repr__", _sprite_repr);
	krk_defineNative(&YutaniSprite->methods, "__init__", _sprite_init);
	krk_finalizeClass(YutaniSprite);

	/**
	 * class Font():
	 *     fontType, fontSize, fontGamma, fontStroke
	 */
	YutaniFont = krk_createClass(module, "Font", NULL);
	YutaniFont->allocSize = sizeof(struct YutaniFont);
	YutaniFont->docstring = S("Font(type,size,gamma=1.7,stroke=0.75,color=color(0,0,0))\n"
		"  Create a Font specification for rendering text.");
	krk_defineNative(&YutaniFont->methods, "__init__", _font_init);
	krk_defineNative(&YutaniFont->methods, "draw_string", _font_draw_string)->doc =
		"Font.draw_string(gfxContext, string, x, y)\n"
		"  Draw text to a graphics context with this font.";
	krk_defineNative(&YutaniFont->methods, "width", _font_width)->doc =
		"Font.width(string)\n"
		"  Calculate the rendered width of the given string when drawn with this font.";
	krk_defineNative(&YutaniFont->methods, "size", _font_size)->flags |= KRK_NATIVE_FLAGS_IS_DYNAMIC_PROPERTY;
	/* Some static values */
#define ATTACH_FONT(name) krk_attachNamedValue(&YutaniFont->methods, #name, INTEGER_VAL(SDF_ ## name))
	ATTACH_FONT(FONT_THIN);
	ATTACH_FONT(FONT_BOLD);
	ATTACH_FONT(FONT_MONO);
	ATTACH_FONT(FONT_MONO_BOLD);
	ATTACH_FONT(FONT_MONO_OBLIQUE);
	ATTACH_FONT(FONT_MONO_BOLD_OBLIQUE);
	ATTACH_FONT(FONT_OBLIQUE);
	ATTACH_FONT(FONT_BOLD_OBLIQUE);
	krk_finalizeClass(YutaniFont);

	MenuBarClass = krk_createClass(module, "MenuBar", NULL);
	MenuBarClass->allocSize = sizeof(struct MenuBarClass);
	MenuBarClass->_ongcsweep = _MenuBar_gcsweep;
	krk_defineNative(&MenuBarClass->methods, "__init__", _MenuBar_init);
	krk_defineNative(&MenuBarClass->methods, "place", _MenuBar_place);
	krk_defineNative(&MenuBarClass->methods, "render", _MenuBar_render);
	krk_defineNative(&MenuBarClass->methods, "mouse_event", _MenuBar_mouse_event);
	krk_defineNative(&MenuBarClass->methods, "insert", _MenuBar_insert);
	krk_finalizeClass(MenuBarClass);

	MenuListClass = krk_createClass(module, "MenuList", NULL);
	MenuListClass->allocSize = sizeof(struct MenuListClass);
	krk_defineNative(&MenuListClass->methods, "__init__", _MenuList_init);
	krk_defineNative(&MenuListClass->methods, "insert", _MenuList_insert);
	krk_finalizeClass(MenuListClass);

	MenuEntryClass = krk_createClass(module, "MenuEntry", NULL);
	MenuEntryClass->allocSize = sizeof(struct MenuEntryClass);
	krk_defineNative(&MenuEntryClass->methods, "__init__", _MenuEntry_init);
	krk_finalizeClass(MenuEntryClass);

	MenuEntrySubmenuClass = krk_createClass(module, "MenuEntrySubmenu", MenuEntryClass);
	krk_defineNative(&MenuEntrySubmenuClass->methods, "__init__", _MenuEntrySubmenu_init);
	krk_finalizeClass(MenuEntrySubmenuClass);
	MenuEntrySeparatorClass = krk_createClass(module, "MenuEntrySeparator", MenuEntryClass);
	krk_defineNative(&MenuEntrySeparatorClass->methods, "__init__", _MenuEntrySeparator_init);
	krk_finalizeClass(MenuEntrySeparatorClass);

	KrkInstance * Decorator = krk_newInstance(vm.baseClasses->objectClass);
	krk_attachNamedObject(&module->fields, "Decorator", (KrkObj*)Decorator);
	krk_defineNative(&Decorator->fields, "get_bounds", _decor_get_bounds);
	krk_defineNative(&Decorator->fields, "render", _decor_render);
	krk_defineNative(&Decorator->fields, "handle_event", _decor_handle_event);
	krk_defineNative(&Decorator->fields, "show_default_menu", _decor_show_default_menu);
#define ATTACH_CONSTANT(name) krk_attachNamedValue(&Decorator->fields, #name, INTEGER_VAL(name))
	ATTACH_CONSTANT(DECOR_OTHER);
	ATTACH_CONSTANT(DECOR_CLOSE);
	ATTACH_CONSTANT(DECOR_RESIZE);
	ATTACH_CONSTANT(DECOR_MAXIMIZE);
	ATTACH_CONSTANT(DECOR_RIGHT);

	ATTACH_CONSTANT(DECOR_ACTIVE);
	ATTACH_CONSTANT(DECOR_INACTIVE);

	ATTACH_CONSTANT(DECOR_FLAG_DECORATED);
	ATTACH_CONSTANT(DECOR_FLAG_NO_MAXIMIZE);
	ATTACH_CONSTANT(DECOR_FLAG_TILED);
	ATTACH_CONSTANT(DECOR_FLAG_TILE_LEFT);
	ATTACH_CONSTANT(DECOR_FLAG_TILE_RIGHT);
	ATTACH_CONSTANT(DECOR_FLAG_TILE_UP);
	ATTACH_CONSTANT(DECOR_FLAG_TILE_DOWN);
#undef ATTACH_CONSTANT

	/* Pop the module object before returning; it'll get pushed again
	 * by the VM before the GC has a chance to run, so it's safe. */
	assert(AS_INSTANCE(krk_pop()) == module);
	return OBJECT_VAL(module);
}

