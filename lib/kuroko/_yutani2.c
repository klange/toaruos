#include <errno.h>
#include <sys/fswait.h>
#include <toaru/yutani.h>
#include <toaru/decorations.h>
#include <toaru/menu.h>
#include <toaru/text.h>
#include <toaru/button.h>

#include <kuroko/kuroko.h>
#include <kuroko/vm.h>
#include <kuroko/util.h>

static KrkInstance * module;

#define CURRENT_NAME self

#define WRAP_TYPE(kname,yname,pname,...) \
	static KrkClass * kname; \
	struct _yutani_ ## kname { \
		KrkInstance inst; \
		yname * pname; \
		__VA_ARGS__ \
	}; \
	__attribute__((unused)) \
	static int _init_check_ ## kname (struct _yutani_ ## kname * self) { \
		return self->pname != NULL; \
	}

#define NO_REINIT(type) do { if (_init_check_ ## type (self)) return krk_runtimeError(vm.exceptions->typeError, "Can not reinit " #type); } while (0)
#define INIT_CHECK(type) do { if (!_init_check_ ## type (self)) return krk_runtimeError(vm.exceptions->typeError, #type " object uninitialized"); } while (0)

WRAP_TYPE(Message,yutani_msg_t,msg);
#define IS_Message(o) (krk_isInstanceOf(o,Message))
#define AS_Message(o) ((struct _yutani_Message*)AS_OBJECT(o))
static void _Message_gcsweep(KrkInstance * _self) {
	struct _yutani_Message * self = (void*)_self;
	free(self->msg);
}

#define CURRENT_CTYPE struct _yutani_Message*

KRK_StaticMethod(Message,__new__) {
	return krk_runtimeError(vm.exceptions->typeError, "can not instantiate Message");
}

/* Base type stuff */
KRK_Method(Message,msg_magic) {
	ATTRIBUTE_NOT_ASSIGNABLE();
	return INTEGER_VAL(self->msg->magic);
}

KRK_Method(Message,msg_type) {
	ATTRIBUTE_NOT_ASSIGNABLE();
	return INTEGER_VAL(self->msg->type);
}

KRK_Method(Message,msg_size) {
	ATTRIBUTE_NOT_ASSIGNABLE();
	return INTEGER_VAL(self->msg->size);
}

KRK_Method(Message,__repr__) {
	struct StringBuilder sb = {0};

	pushStringBuilder(&sb,'<');

	const char * typeName = krk_typeName(argv[0]);
	pushStringBuilderStr(&sb, typeName, strlen(typeName));

	pushStringBuilder(&sb, '>');
	return finishStringBuilder(&sb);
}

#undef CURRENT_CTYPE

/* Subclass stuff */
#define MSG_CLS(type) Message_ ## type
#define WRAP_PROP_INT(cls,kname) KRK_Method(cls,kname) { return INTEGER_VAL(self->kname); }
#define WRAP_PROP_BOOL(cls,kname) KRK_Method(cls,kname) { return BOOLEAN_VAL(self->kname); }

static KrkClass * MSG_CLS(Welcome);
#define AS_Message_Welcome(o) ((struct yutani_msg_welcome*)AS_Message(o)->msg->data)
#define IS_Message_Welcome(o) (krk_isInstanceOf(o,MSG_CLS(Welcome)))
#define CURRENT_CTYPE struct yutani_msg_welcome *
WRAP_PROP_INT(Message_Welcome,display_width)
WRAP_PROP_INT(Message_Welcome,display_height)
#undef CURRENT_CTYPE

static KrkClass * MSG_CLS(WindowMouseEvent);
#define AS_Message_WindowMouseEvent(o) ((struct yutani_msg_window_mouse_event*)AS_Message(o)->msg->data)
#define IS_Message_WindowMouseEvent(o) (krk_isInstanceOf(o,MSG_CLS(WindowMouseEvent)))
#define CURRENT_CTYPE struct yutani_msg_window_mouse_event *
WRAP_PROP_INT(Message_WindowMouseEvent,wid)
WRAP_PROP_INT(Message_WindowMouseEvent,new_x)
WRAP_PROP_INT(Message_WindowMouseEvent,new_y)
WRAP_PROP_INT(Message_WindowMouseEvent,old_x)
WRAP_PROP_INT(Message_WindowMouseEvent,old_y)
WRAP_PROP_INT(Message_WindowMouseEvent,buttons)
WRAP_PROP_INT(Message_WindowMouseEvent,command)
WRAP_PROP_INT(Message_WindowMouseEvent,modifiers)
#undef CURRENT_CTYPE

#define AS_Message_WindowFocusChange(o) ((struct yutani_msg_window_focus_change*)AS_Message(o)->msg->data)
#define IS_Message_WindowFocusChange(o) (krk_isInstanceOf(o,MSG_CLS(WindowFocusChange)))
#define CURRENT_CTYPE struct yutani_msg_window_focus_change *
static KrkClass * MSG_CLS(WindowFocusChange);
WRAP_PROP_INT(Message_WindowFocusChange,wid)
WRAP_PROP_BOOL(Message_WindowFocusChange,focused)
#undef CURRENT_CTYPE

#define AS_Message_ResizeOffer(o) ((struct yutani_msg_window_resize*)AS_Message(o)->msg->data)
#define IS_Message_ResizeOffer(o) (krk_isInstanceOf(o,MSG_CLS(ResizeOffer)))
#define CURRENT_CTYPE struct yutani_msg_window_resize *
static KrkClass * MSG_CLS(ResizeOffer);
WRAP_PROP_INT(Message_ResizeOffer,wid)
WRAP_PROP_INT(Message_ResizeOffer,width)
WRAP_PROP_INT(Message_ResizeOffer,height)
WRAP_PROP_INT(Message_ResizeOffer,bufid)
#undef CURRENT_CTYPE

#define AS_Message_WindowAdvertise(o) ((struct yutani_msg_window_advertise*)AS_Message(o)->msg->data)
#define IS_Message_WindowAdvertise(o) (krk_isInstanceOf(o,MSG_CLS(WindowAdvertise)))
#define CURRENT_CTYPE struct yutani_msg_window_advertise *
static KrkClass * MSG_CLS(WindowAdvertise);
WRAP_PROP_INT(Message_WindowAdvertise,wid)
WRAP_PROP_INT(Message_WindowAdvertise,flags)
WRAP_PROP_INT(Message_WindowAdvertise,size)
WRAP_PROP_INT(Message_WindowAdvertise,width)
WRAP_PROP_INT(Message_WindowAdvertise,height)
WRAP_PROP_INT(Message_WindowAdvertise,bufid)
KRK_Method(Message_WindowAdvertise,name) {
	char * s = self->strings;
	size_t l = strlen(s);
	return OBJECT_VAL(krk_copyString(s,l));
}
KRK_Method(Message_WindowAdvertise,icon) {
	char * s = self->strings + self->icon;
	size_t l = strlen(s);
	return OBJECT_VAL(krk_copyString(s,l));
}
#undef CURRENT_CTYPE

#define AS_Message_WindowMove(o) ((struct yutani_msg_window_move*)AS_Message(o)->msg->data)
#define IS_Message_WindowMove(o) (krk_isInstanceOf(o,MSG_CLS(WindowMove)))
#define CURRENT_CTYPE struct yutani_msg_window_move *
static KrkClass * MSG_CLS(WindowMove);
WRAP_PROP_INT(Message_WindowMove,wid)
WRAP_PROP_INT(Message_WindowMove,x)
WRAP_PROP_INT(Message_WindowMove,y)
#undef CURRENT_CTYPE

#define AS_Message_KeyEvent(o) ((struct yutani_msg_key_event*)AS_Message(o)->msg->data)
#define IS_Message_KeyEvent(o) (krk_isInstanceOf(o,MSG_CLS(KeyEvent)))
#define CURRENT_CTYPE struct yutani_msg_key_event *
static KrkClass * MSG_CLS(KeyEvent);
WRAP_PROP_INT(Message_KeyEvent,wid)

#define WRAP_PROP_FROM(f,p) KRK_Method(Message_KeyEvent,p) { return INTEGER_VAL(self->f.p); }
WRAP_PROP_FROM(event,keycode)
WRAP_PROP_FROM(event,modifiers)
WRAP_PROP_FROM(event,action)
WRAP_PROP_FROM(event,key)
WRAP_PROP_FROM(state,kbd_state)
WRAP_PROP_FROM(state,kbd_s_state)
WRAP_PROP_FROM(state,k_ctrl)
WRAP_PROP_FROM(state,k_shift)
WRAP_PROP_FROM(state,k_alt)
WRAP_PROP_FROM(state,k_super)
WRAP_PROP_FROM(state,kl_ctrl)
WRAP_PROP_FROM(state,kl_shift)
WRAP_PROP_FROM(state,kl_alt)
WRAP_PROP_FROM(state,kl_super)
WRAP_PROP_FROM(state,kr_ctrl)
WRAP_PROP_FROM(state,kr_shift)
WRAP_PROP_FROM(state,kr_alt)
WRAP_PROP_FROM(state,kr_super)
WRAP_PROP_FROM(state,kbd_esc_buf)
#undef CURRENT_CTYPE

#define AS_Message_WindowClose(o) ((struct yutani_msg_key_event*)AS_Message(o)->msg->data)
#define IS_Message_WindowClose(o) (krk_isInstanceOf(o,MSG_CLS(WindowClose)))
#define CURRENT_CTYPE struct yutani_msg_key_event *
static KrkClass * MSG_CLS(WindowClose);
WRAP_PROP_INT(Message_WindowClose,wid)
#undef CURRENT_CTYPE

WRAP_TYPE(YutaniCtx,yutani_t,yctx);
#define AS_YutaniCtx(o) ((struct _yutani_YutaniCtx*)AS_OBJECT(o))
#define IS_YutaniCtx(o) (krk_isInstanceOf(o,YutaniCtx))
#define CURRENT_CTYPE struct _yutani_YutaniCtx*

static struct _yutani_YutaniCtx * yctxInstance;
KRK_StaticMethod(YutaniCtx,__new__) {
	if (yctxInstance) return OBJECT_VAL(yctxInstance);

	KrkClass * cls;
	if (!krk_parseArgs("O!:YutaniCtx", (const char*[]){"cls"}, KRK_BASE_CLASS(type), &cls)) {
		return NONE_VAL();
	}

	CURRENT_CTYPE self = (CURRENT_CTYPE)krk_newInstance(cls);
	krk_push(OBJECT_VAL(self));

	yutani_t * yctx = yutani_init();
	if (!yctx) return krk_runtimeError(vm.exceptions->ioError, "Failed to connect to compositor.");
	yctxInstance = self;
	self->yctx = yctx;
	init_decorations();
	krk_attachNamedObject(&module->fields, "_yutani_t", (KrkObj*)self);

	return krk_pop();
}

KRK_Method(YutaniCtx,display_width) { return INTEGER_VAL(self->yctx->display_width); }
KRK_Method(YutaniCtx,display_height) { return INTEGER_VAL(self->yctx->display_height); }

static KrkValue makeMessage(yutani_msg_t * result) {
	if (!result) return NONE_VAL();

	KrkClass * msgType = Message;
	switch (result->type) {
		case YUTANI_MSG_WELCOME:
			msgType = Message_Welcome;
			break;
		case YUTANI_MSG_WINDOW_MOUSE_EVENT:
			msgType = Message_WindowMouseEvent;
			break;
		case YUTANI_MSG_WINDOW_FOCUS_CHANGE:
			msgType = Message_WindowFocusChange;
			break;
		case YUTANI_MSG_RESIZE_OFFER:
			msgType = Message_ResizeOffer;
			break;
		case YUTANI_MSG_WINDOW_ADVERTISE:
			msgType = Message_WindowAdvertise;
			break;
		case YUTANI_MSG_WINDOW_MOVE:
			msgType = Message_WindowMove;
			break;
		case YUTANI_MSG_KEY_EVENT:
			msgType = Message_KeyEvent;
			break;
		case YUTANI_MSG_WINDOW_CLOSE:
			msgType = Message_WindowClose;
			break;

		default: break;
	}

	struct _yutani_Message * out = (void*)krk_newInstance(msgType);
	out->msg = result;
	return OBJECT_VAL(out);
}

KRK_Method(YutaniCtx,poll) {
	int sync = 1;

	if (!krk_parseArgs(".|p", (const char *[]){"sync"}, &sync)) return NONE_VAL();

	yutani_msg_t * result = sync ? yutani_poll(self->yctx) : yutani_poll_async(self->yctx);

	return makeMessage(result);
}

KRK_Method(YutaniCtx,wait_for) {
	int msgtype;
	if (!krk_parseArgs(".i",(const char*[]){"msgtype"}, &msgtype)) return NONE_VAL();
	yutani_msg_t * result = yutani_wait_for(self->yctx, msgtype);
	return makeMessage(result);
}

KRK_Method(YutaniCtx,subscribe) {
	yutani_subscribe_windows(self->yctx);
	return NONE_VAL();
}

KRK_Method(YutaniCtx,unsubscribe) {
	yutani_unsubscribe_windows(self->yctx);
	return NONE_VAL();
}

KRK_Method(YutaniCtx,query_windows) {
	yutani_query_windows(self->yctx);
	return NONE_VAL();
}

KRK_Method(YutaniCtx,fileno) {
	return INTEGER_VAL(fileno(self->yctx->sock));
}

KRK_Method(YutaniCtx,query) {
	return INTEGER_VAL(yutani_query(self->yctx));
}

KRK_Method(YutaniCtx,menu_process_event) {
	struct _yutani_Message * message = NULL;
	if (!krk_parseArgs(".O!",(const char *[]){"message"}, Message, &message)) return NONE_VAL();
	return INTEGER_VAL(menu_process_event(self->yctx, message->msg));
}

#undef CURRENT_CTYPE

WRAP_TYPE(GraphicsContext,gfx_context_t,ctx,
	int doubleBuffered;
);

WRAP_TYPE(Sprite,gfx_context_t,ctx,
	int doubleBuffered;
	sprite_t * sprite;
);

WRAP_TYPE(Window,gfx_context_t,ctx,
	int doubleBuffered;
	yutani_window_t * window;
	KrkValue title;
	KrkValue icon;
	int closed;
);

WRAP_TYPE(Subregion,gfx_context_t,ctx,
	int doubleBuffered;
	int x;
	int y;
);

#define IS_GraphicsContext(o) (krk_isInstanceOf(o,GraphicsContext))
#define AS_GraphicsContext(o) ((struct _yutani_GraphicsContext*)AS_OBJECT(o))
#define CURRENT_CTYPE struct _yutani_GraphicsContext*

#define CHECK_GFX() do { if (!self->ctx) return krk_runtimeError(vm.exceptions->valueError, "invalid context"); } while (0)

KRK_StaticMethod(GraphicsContext,__new__) {
	KrkClass * cls;
	int _argc;
	const KrkValue * _argv;

	if (!krk_parseArgs("O!*~",(const char*[]){"cls"},KRK_BASE_CLASS(type),&cls,&_argc,&_argv)) return NONE_VAL();

	if (!krk_isSubClass(cls, GraphicsContext)) {
		return krk_runtimeError(vm.exceptions->typeError, "%S is not a subclass of GraphicsContext", cls->name);
	}

	if (cls == GraphicsContext) {
		return krk_runtimeError(vm.exceptions->typeError, "Can not create GraphicsContext");
	}

	return OBJECT_VAL(krk_newInstance(cls));
}

KRK_Method(GraphicsContext,fill) {
	uint32_t color;
	if (!krk_parseArgs(".I",(const char*[]){"color"},&color)) return NONE_VAL();
	CHECK_GFX();
	draw_fill(self->ctx, color);
	return NONE_VAL();
}

KRK_Method(GraphicsContext,flip) {
	CHECK_GFX();
	if (self->doubleBuffered) {
		flip(self->ctx);
	}
	return NONE_VAL();
}

KRK_Method(GraphicsContext,blur) {
	int radius = 2;
	if (!krk_parseArgs(".|I",(const char*[]){"radius"}, &radius)) return NONE_VAL();
	CHECK_GFX();
	blur_context_box(self->ctx, radius);
	return NONE_VAL();
}

KRK_Method(GraphicsContext,line) {
	int x0, x1, y0, y1;
	uint32_t color;
	KrkValue thickness = NONE_VAL();

	if (!krk_parseArgs(".iiiiI|V",(const char*[]){"x0","x1","y0","y1","color","thickness"},
		&x0, &x1, &y0, &y1, &color, &thickness)) return NONE_VAL();

	if (IS_NONE(thickness)) {
		draw_line(self->ctx, x0, x1, y0, y1, color);
	} else if (IS_INTEGER(thickness)) {
		draw_line_thick(self->ctx, x0, x1, y0, y1, color, AS_INTEGER(thickness));
	} else if (IS_FLOATING(thickness)) {
		draw_line_aa(self->ctx, x0, x1, y0, y1, color, AS_FLOATING(thickness));
	} else {
		TYPE_ERROR(int or float,thickness);
	}
	return NONE_VAL();
}

KRK_Method(GraphicsContext,rect) {
	int x, y;
	unsigned int width, height;
	uint32_t color;
	int solid = 0;
	unsigned int radius = 0;

	if (!krk_parseArgs(".iiIII|pI",(const char*[]){"x","y","width","height","color","solid","radius"},
		&x, &y, &width, &height, &color, &solid, &radius)) return NONE_VAL();

	if (solid && radius) {
		return krk_runtimeError(vm.exceptions->valueError, "can not combine 'radius' and 'solid'");
	}

	CHECK_GFX();

	if (radius) {
		draw_rounded_rectangle(self->ctx, x, y, width, height, radius, color);
	} else if (solid) {
		draw_rectangle_solid(self->ctx, x, y, width, height, color);
	} else {
		draw_rectangle(self->ctx, x, y, width, height, color);
	}

	return NONE_VAL();
}

KRK_Method(GraphicsContext,width) { CHECK_GFX(); return INTEGER_VAL(self->ctx->width); }
KRK_Method(GraphicsContext,height) { CHECK_GFX(); return INTEGER_VAL(self->ctx->height); }
KRK_Method(GraphicsContext,isDoubleBuffered) { CHECK_GFX(); return BOOLEAN_VAL(self->doubleBuffered); }

KRK_Method(GraphicsContext,draw_sprite) {
	CHECK_GFX();

	struct _yutani_Sprite * sprite;
	int x = 0;
	int y = 0;
	double alpha = 1.0;
	double rotation = 0.0;
	KrkTuple * scale = NULL;
	uint32_t color = 0;

	if (!krk_parseArgs(
		".O!|iiddO!I",(const char*[]){"sprite","x","y","alpha","rotation","scale","color"},
		Sprite, &sprite,
		&x, &y,
		&alpha, &rotation,
		KRK_BASE_CLASS(tuple), &scale,
		&color)) {
		return NONE_VAL();
	}

	if (scale) {
		if (scale->values.count != 2 || !IS_INTEGER(scale->values.values[0]) || !IS_INTEGER(scale->values.values[1])) {
			return krk_runtimeError(vm.exceptions->typeError, "scale must be (int,int)");
		}

		int32_t width  = AS_INTEGER(scale->values.values[0]);
		int32_t height = AS_INTEGER(scale->values.values[1]);

		if (alpha == 1.0) {
			draw_sprite_scaled(self->ctx, sprite->sprite, x, y, width, height);
		} else {
			draw_sprite_scaled_alpha(self->ctx, sprite->sprite, x, y, width, height, alpha);
		}
	} else if (color) {
		draw_sprite_alpha_paint(self->ctx, sprite->sprite, x, y, alpha, color);
	} else if (rotation != 0.0) {
		draw_sprite_rotate(self->ctx, sprite->sprite, x, y, rotation, alpha);
	} else if (alpha == 1.0) {
		draw_sprite(self->ctx, sprite->sprite, x, y);
	} else {
		draw_sprite_alpha(self->ctx, sprite->sprite, x, y, alpha);
	}

	return NONE_VAL();
}

#undef CURRENT_CTYPE

static void _yutani_Sprite_gcsweep(KrkInstance * _self) {
	struct _yutani_Sprite * self = (void*)_self;
	if (self->sprite) sprite_free(self->sprite);
	if (self->ctx) release_graphics_yutani(self->ctx);
}

#define IS_Sprite(o) (krk_isInstanceOf(o,Sprite))
#define AS_Sprite(o) ((struct _yutani_Sprite*)AS_OBJECT(o))
#define CURRENT_CTYPE struct _yutani_Sprite*

/**
 * Sprite(file=None,*,width=0,height=0)
 *
 * Either file or width/height need to be specified, but not both.
 */
KRK_Method(Sprite,__init__) {
	const char * filename = NULL;
	unsigned int width = 0;
	unsigned int height = 0;

	if (!krk_parseArgs(
		".|z$II:Sprite",(const char*[]){"file","width","height"},
		&filename,
		&width,
		&height
	)) {
		return NONE_VAL();
	}

	if ((!filename && (!width || !height)) || (filename && (width || height))) {
		return krk_runtimeError(vm.exceptions->argumentError,
			"Either 'file' or both of 'width' and 'height' must be provided, but not both.");
	}

	NO_REINIT(Sprite);

	sprite_t * sprite;

	/* Set up sprite */
	if (!filename) {
		/* Want to build a new sprite */
		sprite = create_sprite(width, height, ALPHA_EMBEDDED);
	} else {
		sprite = malloc(sizeof(sprite_t));
		if (load_sprite(sprite, filename)) {
			free(sprite);
			return krk_runtimeError(vm.exceptions->ioError, "could not load sprite from '%s'", filename);
		}
	}

	/* Initialize representative graphics context */
	gfx_context_t * ctx = init_graphics_sprite(sprite);

	self->ctx = ctx;
	self->sprite = sprite;

	/* Keep the file if we had one */
	krk_attachNamedValue(&self->inst.fields, "file",
		filename ? OBJECT_VAL(krk_copyString(filename,strlen(filename))) : NONE_VAL());

	return NONE_VAL();
}

KRK_Method(Sprite,__repr__) {
	KrkValue file = NONE_VAL();
	krk_tableGet_fast(&self->inst.fields, S("file"), &file);

	INIT_CHECK(Sprite);

	if (!IS_NONE(file)) {
		return krk_stringFromFormat("Sprite(file=%R,width=%u,height=%u)",
			file, self->sprite->width, self->sprite->height);
	} else {
		return krk_stringFromFormat("Sprite(width=%u,height=%u)",
			self->sprite->width, self->sprite->height);
	}
}

KRK_Method(Sprite,free) {
	INIT_CHECK(Sprite);
	if (self->sprite) sprite_free(self->sprite);
	if (self->ctx) release_graphics_yutani(self->ctx);
	self->sprite = NULL;
	self->ctx = NULL;
	return NONE_VAL();
}

#undef CURRENT_CTYPE

static void _yutani_Window_gcscan(KrkInstance * _self) {
	struct _yutani_Window * self = (void*)_self;
	krk_markValue(self->title);
	krk_markValue(self->icon);
}

#define IS_Window(o) (krk_isInstanceOf(o,Window))
#define AS_Window(o) ((struct _yutani_Window*)AS_OBJECT(o))
#define CURRENT_CTYPE struct _yutani_Window*

static void update_window_title(struct _yutani_Window * self) {
	if (IS_STRING(self->title)) {
		if (IS_STRING(self->icon)) {
			yutani_window_advertise_icon(yctxInstance->yctx, self->window, AS_CSTRING(self->title), AS_CSTRING(self->icon));
		} else {
			yutani_window_advertise(yctxInstance->yctx, self->window, AS_CSTRING(self->title));
		}
	}

	/* TODO Update decorations */
}

KRK_Method(Window,__init__) {
	unsigned int width;
	unsigned int height;
	unsigned int flags = 0;
	KrkValue title = NONE_VAL();
	KrkValue icon = NONE_VAL();
	int doublebuffer = 1;

	if (!yctxInstance) return krk_runtimeError(vm.exceptions->valueError, "Compositor is not initialized");

	if (!krk_parseArgs(
		".II|IV!V!p:Window",(const char *[]){"width","height","flags","title","icon","doublebuffer"},
		&width, &height,
		&flags,
		KRK_BASE_CLASS(str), &title,
		KRK_BASE_CLASS(str), &icon,
		&doublebuffer
	)) {
		return NONE_VAL();
	}

	NO_REINIT(Window);

	self->window = yutani_window_create_flags(yctxInstance->yctx, width, height, flags);
	self->doubleBuffered = doublebuffer;
	self->ctx = doublebuffer ? init_graphics_yutani_double_buffer(self->window) : init_graphics_yutani(self->window);
	self->title = title;
	self->icon = icon;
	self->closed = 0;

	update_window_title(self);

	return NONE_VAL();
}

KRK_Method(Window,title) {
	INIT_CHECK(Window);
	if (argc > 1) {
		if (!IS_STRING(argv[1]) && !IS_NONE(argv[1])) return TYPE_ERROR(str,argv[1]);
		self->title = argv[1];
		update_window_title(self);
	}
	return self->title;
}

KRK_Method(Window,icon) {
	INIT_CHECK(Window);
	if (argc > 1) {
		if (!IS_STRING(argv[1]) && !IS_NONE(argv[1])) return TYPE_ERROR(str,argv[1]);
		self->icon = argv[1];
		update_window_title(self);
	}

	return self->icon;
}

KRK_Method(Window,wid) { INIT_CHECK(Window); return INTEGER_VAL(self->window->wid); }
KRK_Method(Window,x) { INIT_CHECK(Window); return INTEGER_VAL(self->window->x); }
KRK_Method(Window,y) { INIT_CHECK(Window); return INTEGER_VAL(self->window->y); }
KRK_Method(Window,focused) {
	INIT_CHECK(Window);

	if (argc > 1) {
		if (!IS_BOOLEAN(argv[1])) return krk_runtimeError(vm.exceptions->typeError, "focused must be bool, not %T", argv[1]);
		self->window->focused = AS_BOOLEAN(argv[1]);
	}

	return BOOLEAN_VAL(self->window->focused);
}

KRK_Method(Window,closed) { return INTEGER_VAL(self->closed); }

KRK_Method(Window,__repr__) {
	INIT_CHECK(Window);
	if (!self->window) {
		return krk_stringFromFormat("Window(title=%R,closed=True)", self->title);
	}
	return krk_stringFromFormat("Window(wid=%d,title=%R,width=%d,height=%d)",
		self->window->wid, self->title, self->window->width, self->window->height);
}

KRK_Method(Window,flip) {
	INIT_CHECK(Window);
	if (self->doubleBuffered) {
		flip(self->ctx);
	}
	yutani_flip(yctxInstance->yctx, self->window);
	return NONE_VAL();
}

KRK_Method(Window,move) {
	int x, y;
	if (!krk_parseArgs(".ii", (const char*[]){"x","y"}, &x, &y)) return NONE_VAL();
	INIT_CHECK(Window);
	yutani_window_move(yctxInstance->yctx, self->window, x, y);
	return NONE_VAL();
}

KRK_Method(Window,close) {
	INIT_CHECK(Window);
	yutani_close(yctxInstance->yctx, self->window);
	self->window = NULL;
	release_graphics_yutani(self->ctx);
	self->ctx = NULL;
	self->closed = 1;
	return NONE_VAL();
}

KRK_Method(Window,set_stack) {
	unsigned int z;
	if (!krk_parseArgs(".I", (const char*[]){"z"}, &z)) return NONE_VAL();
	INIT_CHECK(Window);
	yutani_set_stack(yctxInstance->yctx, self->window, z);
	return NONE_VAL();
}

KRK_Method(Window,special_request) {
	unsigned int request;
	if (!krk_parseArgs(".I", (const char*[]){"request"}, &request)) return NONE_VAL();
	INIT_CHECK(Window);
	yutani_special_request(yctxInstance->yctx, self->window, request);
	return NONE_VAL();
}

KRK_Method(Window,resize) {
	unsigned int width, height;
	if (!krk_parseArgs(".II", (const char*[]){"width","height"}, &width, &height)) return NONE_VAL();
	INIT_CHECK(Window);
	yutani_window_resize(yctxInstance->yctx, self->window, width, height);
	return NONE_VAL();
}

KRK_Method(Window,resize_start) {
	unsigned int direction;
	if (!krk_parseArgs(".I", (const char*[]){"direction"}, &direction)) return NONE_VAL();
	INIT_CHECK(Window);
	yutani_window_resize_start(yctxInstance->yctx, self->window, direction);
	return NONE_VAL();
}

KRK_Method(Window,resize_done) {
	INIT_CHECK(Window);
	yutani_window_resize_done(yctxInstance->yctx, self->window);
	return NONE_VAL();
}

KRK_Method(Window,resize_offer) {
	unsigned int width, height;
	if (!krk_parseArgs(".II", (const char*[]){"width","height"}, &width, &height)) return NONE_VAL();
	INIT_CHECK(Window);
	yutani_window_resize_offer(yctxInstance->yctx, self->window, width, height);
	return NONE_VAL();
}

KRK_Method(Window,resize_accept) {
	unsigned int width, height;
	if (!krk_parseArgs(".II", (const char*[]){"width","height"}, &width, &height)) return NONE_VAL();
	INIT_CHECK(Window);
	yutani_window_resize_accept(yctxInstance->yctx, self->window, width, height);
	return NONE_VAL();
}

KRK_Method(Window,update_shape) {
	unsigned int threshold;
	if (!krk_parseArgs(".I", (const char*[]){"threshold"}, &threshold)) return NONE_VAL();
	INIT_CHECK(Window);
	yutani_window_update_shape(yctxInstance->yctx, self->window, threshold);
	return NONE_VAL();
}

KRK_Method(Window,show_mouse) {
	unsigned int mouse;
	if (!krk_parseArgs(".I", (const char*[]){"mouse"}, &mouse)) return NONE_VAL();
	INIT_CHECK(Window);
	yutani_window_show_mouse(yctxInstance->yctx, self->window, mouse);
	return NONE_VAL();
}

KRK_Method(Window,warp_mouse) {
	int x, y;
	if (!krk_parseArgs(".ii", (const char*[]){"x","y"}, &x, &y)) return NONE_VAL();
	INIT_CHECK(Window);
	yutani_window_warp_mouse(yctxInstance->yctx, self->window, x, y);
	return NONE_VAL();
}

KRK_Method(Window,reinit) {
	INIT_CHECK(Window);
	reinit_graphics_yutani(self->ctx, self->window);
	return NONE_VAL();
}
#undef CURRENT_CTYPE

static void _yutani_Subregion_gcsweep(KrkInstance * _self) {
	struct _yutani_Subregion * self = (void*)_self;
	if (self->ctx) {
		if (self->ctx->clips) {
			free(self->ctx->clips);
		}
		free(self->ctx);
		self->ctx = NULL;
	}
}

#define IS_Subregion(o) (krk_isInstanceOf(o,Subregion))
#define AS_Subregion(o) ((struct _yutani_Subregion*)AS_OBJECT(o))
#define CURRENT_CTYPE struct _yutani_Subregion*

KRK_Method(Subregion,__init__) {
	struct _yutani_GraphicsContext * ctx;
	int x, y, w, h;
	if (!krk_parseArgs(
		".O!iiii:Subregion", (const char*[]){"ctx","x","y","w","h"},
		GraphicsContext, &ctx,
		&x, &y, &w, &h)) {
		return NONE_VAL();
	}

	NO_REINIT(Subregion);

	if (!ctx->ctx) return krk_runtimeError(vm.exceptions->typeError, "ctx is not initialized");

	if (w < 0 || h < 0) return krk_runtimeError(vm.exceptions->typeError, "invalid subregion");

	if (x < 0) {
		w += x;
		x = 0;
	}

	if (y < 0) {
		h += y;
		y = 0;
	}

	if (x >= ctx->ctx->width || y >= ctx->ctx->height) {
		x = 0; y = 0; w = 0; h = 0;
	}

	if (x + w > ctx->ctx->width) {
		w = ctx->ctx->width - x;
	}

	if (y + h > ctx->ctx->height) {
		h = ctx->ctx->height - y;
	}

	gfx_context_t * sub = init_graphics_subregion(ctx->ctx, x, y, w, h);
	self->ctx = sub;
	self->doubleBuffered = ctx->doubleBuffered;
	self->x = x;
	self->y = y;
	krk_attachNamedObject(&self->inst.fields, "parent", (KrkObj*)ctx);

	return NONE_VAL();
}

KRK_Method(Subregion,offset_x) {
	ATTRIBUTE_NOT_ASSIGNABLE();
	return INTEGER_VAL(self->x);
}

KRK_Method(Subregion,offset_y) {
	ATTRIBUTE_NOT_ASSIGNABLE();
	return INTEGER_VAL(self->y);
}

#undef CURRENT_CTYPE

static KrkClass * TransformMatrix;
struct _yutani_TransformMatrix {
	KrkInstance inst;
	gfx_matrix_t matrix;
};

#define IS_TransformMatrix(o) (krk_isInstanceOf(o,TransformMatrix))
#define AS_TransformMatrix(o) ((struct _yutani_TransformMatrix*)AS_OBJECT(o))
#define CURRENT_CTYPE struct _yutani_TransformMatrix*

KRK_Method(TransformMatrix,__init__) {
	double a = 1.0, b = 0, tx = 0, c = 0, d = 1.0, ty = 0;
	if (!krk_parseArgs(".|dddddd:TransformMatrix",
		(const char*[]){"a","b","tx","c","d","ty"}, 
		&a,&b,&tx,&c,&d,&ty)) return NONE_VAL();
	self->matrix[0][0] = a;
	self->matrix[0][1] = b;
	self->matrix[0][2] = tx;
	self->matrix[1][0] = c;
	self->matrix[1][1] = d;
	self->matrix[1][2] = ty;
	return NONE_VAL();
}

#define MATRIX_VAR(name,row,col) KRK_Method(TransformMatrix,name) { \
	double x = self->matrix[row][col]; \
	if (!krk_parseArgs(".|d",(const char*[]){"val"},&x)) return NONE_VAL(); \
	self->matrix[row][col] = x; \
	return FLOATING_VAL(x); \
}

MATRIX_VAR(a,0,0)
MATRIX_VAR(b,0,1)
MATRIX_VAR(tx,0,1)
MATRIX_VAR(c,1,0)
MATRIX_VAR(d,1,1)
MATRIX_VAR(ty,1,1)

KRK_Method(TransformMatrix,__repr__) {
	struct StringBuilder sb = {};

	KrkValue floats[6];

	for (int i = 0; i < 6; ++i) {
		floats[i] = FLOATING_VAL(self->matrix[i/3][i%3]);
	}

	krk_pushStringBuilderFormat(&sb, "TransformMatrix[ [%R,%R,%R] [%R,%R,%R] ]",
		floats[0], floats[1], floats[2], floats[3], floats[4], floats[5]);

	return krk_finishStringBuilder(&sb);
}

KRK_Method(TransformMatrix,scale) {
	double x, y;
	if (!krk_parseArgs(".dd", (const char*[]){"x","y"}, &x, &y)) return NONE_VAL();
	gfx_matrix_scale(self->matrix,x,y);
	return NONE_VAL();
}

KRK_Method(TransformMatrix,translate) {
	double x, y;
	if (!krk_parseArgs(".dd", (const char*[]){"x","y"}, &x, &y)) return NONE_VAL();
	gfx_matrix_translate(self->matrix,x,y);
	return NONE_VAL();
}

KRK_Method(TransformMatrix,rotate) {
	double r;
	if (!krk_parseArgs(".d", (const char*[]){"r"}, &r)) return NONE_VAL();
	gfx_matrix_rotate(self->matrix,r);
	return NONE_VAL();
}

KRK_Method(TransformMatrix,shear) {
	double x, y;
	if (!krk_parseArgs(".dd", (const char*[]){"x","y"}, &x, &y)) return NONE_VAL();
	gfx_matrix_shear(self->matrix,x,y);
	return NONE_VAL();
}

KRK_Method(TransformMatrix,apply) {
	double x, y;
	if (!krk_parseArgs(".dd", (const char*[]){"x","y"}, &x, &y)) return NONE_VAL();
	KrkTuple * out = krk_newTuple(2);
	krk_push(OBJECT_VAL(out));

	double o_x, o_y;
	gfx_apply_matrix(x,y,self->matrix,&o_x,&o_y);
	out->values.values[out->values.count++] = FLOATING_VAL(o_x);
	out->values.values[out->values.count++] = FLOATING_VAL(o_y);

	return krk_pop();
}

#undef CURRENT_CTYPE

WRAP_TYPE(Font,struct TT_Font,fontData,
	int fontSize;
	uint32_t fontColor;
);

#define IS_Font(o) (krk_isInstanceOf(o,Font))
#define AS_Font(o) ((struct _yutani_Font*)AS_OBJECT(o))
#define CURRENT_CTYPE struct _yutani_Font*

WRAP_TYPE(TTShape,struct TT_Shape, shape);
#define IS_TTShape(o) (krk_isInstanceOf(o,TTShape))
#define AS_TTShape(o) ((struct _yutani_TTShape*)AS_OBJECT(o))

WRAP_TYPE(TTContour,struct TT_Contour, contour);
#define IS_TTContour(o) (krk_isInstanceOf(o,TTContour))
#define AS_TTContour(o) ((struct _yutani_TTContour*)AS_OBJECT(o))

#define CHECK_FONT() do { if (!self->fontData) return krk_runtimeError(vm.exceptions->valueError, "font is uninitialized"); } while (0)

static void _yutani_Font_gcsweep(KrkInstance * _self) {
	CURRENT_CTYPE self = (CURRENT_CTYPE)_self;
	if (self->fontData) free(self->fontData);
}

KRK_Method(Font,__init__) {
	const char * filename;
	int size;
	uint32_t color = rgb(0,0,0);

	if (!krk_parseArgs(
		".si|I:Font",(const char*[]){"font","size","color"},
		&filename, &size, &color)) {
		return NONE_VAL();
	}

	NO_REINIT(Font);

	struct TT_Font * fd;

	if (strstr(filename, "sans-serif") == filename || strstr(filename, "monospace") == filename) {
		fd = tt_font_from_shm(filename);
	} else {
		fd = tt_font_from_file(filename);
	}

	if (!fd) {
		return krk_runtimeError(vm.exceptions->ioError, "failed to load '%s'", filename);
	}

	tt_set_size(fd, size);

	self->fontData = fd;
	self->fontSize = size;
	self->fontColor = color;

	krk_attachNamedValue(&self->inst.fields, "file", OBJECT_VAL(krk_copyString(filename, strlen(filename))));

	return NONE_VAL();
}

KRK_Method(Font,size) {
	INIT_CHECK(Font);
	if (argc > 1) {
		if (!IS_INTEGER(argv[1])) return krk_runtimeError(vm.exceptions->typeError, "size must be int, not %T", argv[1]);
		self->fontSize = AS_INTEGER(argv[1]);
		tt_set_size(self->fontData, self->fontSize);
	}

	return self->fontSize;
}

KRK_Method(Font,draw_string) {
	struct _yutani_GraphicsContext * ctx;
	const char * s;
	int x;
	int y;

	if (!krk_parseArgs(
		".O!sii", (const char*[]){"ctx","s","x","y"},
		GraphicsContext, &ctx,
		&s, &x, &y)) {
		return NONE_VAL();
	}

	INIT_CHECK(Font);
	return INTEGER_VAL(tt_draw_string(ctx->ctx, self->fontData, x, y, s, self->fontColor));
}

KRK_Method(Font,draw_string_shadow) {
	struct _yutani_GraphicsContext * ctx;
	const char * s;
	int x;
	int y;
	uint32_t shadow;
	int blur;

	if (!krk_parseArgs(
		".O!siiIi", (const char*[]){"ctx","s","x","y","shadow","blur"},
		GraphicsContext, &ctx,
		&s, &x, &y, &shadow, &blur)) {
		return NONE_VAL();
	}

	INIT_CHECK(Font);

	/* This has a weird API for reasons I can't remember */
	tt_draw_string_shadow(ctx->ctx, self->fontData, (char*)s, self->fontSize, x, y, self->fontColor, shadow, blur);

	return NONE_VAL();
}

KRK_Method(Font,width) {
	const char * s;
	if (!krk_parseArgs(".s", (const char*[]){"s"}, &s)) return NONE_VAL();
	INIT_CHECK(Font);
	return INTEGER_VAL(tt_string_width(self->fontData, s));
}

KRK_Method(Font,measure) {
	INIT_CHECK(Font);

	KrkTuple * out = krk_newTuple(3);
	krk_push(OBJECT_VAL(out));

	struct TT_FontMetrics metrics;

	tt_measure_font(self->fontData, &metrics);

	out->values.values[out->values.count++] = FLOATING_VAL(metrics.ascender);
	out->values.values[out->values.count++] = FLOATING_VAL(metrics.descender);
	out->values.values[out->values.count++] = FLOATING_VAL(metrics.lineGap);

	return krk_pop();
}

KRK_Method(Font,draw_glyph_into) {
	INIT_CHECK(Font);

	struct _yutani_TTContour * contour;
	float x, y;
	unsigned int glyph;

	if (!krk_parseArgs(".O!ffI",
		(const char*[]){"contour","x","y","glyph"},
		TTContour, &contour,
		&x, &y, &glyph)) return NONE_VAL();

	if (!contour->contour) return krk_runtimeError(vm.exceptions->typeError, "contour is not initialized");

	/* tt_draw_glyph_into returns potentially-realloc'd contour, but we'll return nothing and
	 * just mutate the passed contour object. */
	contour->contour = tt_draw_glyph_into(contour->contour, self->fontData, x, y, glyph);
	return NONE_VAL();
}

KRK_Method(Font,prepare_string) {
	INIT_CHECK(Font);

	struct _yutani_TTContour * contour = NULL;
	float x, y;
	const char * s;

	if (!krk_parseArgs(".ffs|O!", (const char*[]){"x","y","s","into"},
		&x, &y, &s, TTContour, &contour)) return NONE_VAL();

	float out_width = 0;
	KrkTuple * out_tuple = krk_newTuple(2); /* contour, width */
	krk_push(OBJECT_VAL(out_tuple));

	/* if @c into is unset, make a new one to store result; otherwise, @c into is updated */
	if (!contour) {
		contour = (struct _yutani_TTContour*)krk_newInstance(TTContour);
	}

	contour->contour = tt_prepare_string_into(contour->contour, self->fontData, x, y, s, &out_width);
	out_tuple->values.values[out_tuple->values.count++] = OBJECT_VAL(contour);
	out_tuple->values.values[out_tuple->values.count++] = FLOATING_VAL(out_width);

	return krk_pop();
}

KRK_Method(Font,ellipsify) {
	const char * s;
	int max_width;

	if (!krk_parseArgs(".si", (const char*[]){"s","w"},
		&s, &max_width)) return NONE_VAL();

	int out_width = 0;
	char * out = tt_ellipsify(s, self->fontSize, self->fontData, max_width, &out_width);

	KrkTuple * out_tuple = krk_newTuple(2);
	krk_push(OBJECT_VAL(out_tuple));

	out_tuple->values.values[out_tuple->values.count++] = OBJECT_VAL(krk_copyString(out, strlen(out)));
	out_tuple->values.values[out_tuple->values.count++] = INTEGER_VAL(out_width);

	free(out);

	return krk_pop();
}

#undef CURRENT_CTYPE

WRAP_TYPE(MenuBar,struct menu_bar,menuBar);
WRAP_TYPE(MenuList,struct MenuList, menuList);
WRAP_TYPE(MenuEntry,struct MenuEntry, menuEntry);
WRAP_TYPE(MenuEntrySubmenu,struct MenuEntry, menuEntry);
WRAP_TYPE(MenuEntrySeparator,struct MenuEntry, menuEntry);
WRAP_TYPE(MenuEntryCustom,struct MenuEntry, menuEntry);

#define IS_MenuBar(o) (krk_isInstanceOf(o,MenuBar))
#define AS_MenuBar(o) ((struct _yutani_MenuBar*)AS_OBJECT(o))
#define CURRENT_CTYPE struct _yutani_MenuBar*

static void _yutani_MenuBar_gcsweep(KrkInstance * _self) {
	CURRENT_CTYPE self = (CURRENT_CTYPE)_self;
	if (self->menuBar->entries) {
		for (size_t i = 0; self->menuBar->entries[i].title; ++i) {
			free(self->menuBar->entries[i].title);
			free(self->menuBar->entries[i].action);
		}
		free(self->menuBar->entries);
	}
	free(self->menuBar);
}

static void _menubar_callback(struct menu_bar * _self) {
	CURRENT_CTYPE self = _self->_private;
	KrkValue callback;
	if (krk_tableGet(&self->inst.fields, OBJECT_VAL(S("callback")), &callback)) {
		krk_push(callback);
		krk_push(OBJECT_VAL(self));
		krk_callStack(1);
	}
}

KRK_Method(MenuBar,__init__) {
	KrkTuple * entries;

	if (!krk_parseArgs(
		".O!:MenuBar", (const char*[]){"entries"},
		KRK_BASE_CLASS(tuple), &entries)) {
		return NONE_VAL();
	}

	NO_REINIT(MenuBar);

	size_t count = entries->values.count;
	struct menu_bar * out = calloc(sizeof(struct menu_bar), 1);
	out->entries = calloc(sizeof(struct menu_bar_entries), count + 1);

	for (size_t i = 0; i < count; ++i) {
		KrkValue entry = entries->values.values[i];
		if (!IS_TUPLE(entry) || AS_TUPLE(entry)->values.count != 2 ||
			!IS_STRING(AS_TUPLE(entry)->values.values[0]) || !IS_STRING(AS_TUPLE(entry)->values.values[1])) {
			krk_runtimeError(vm.exceptions->typeError, "entries member should be tuple[str,str], not %T", entry);
			goto _error;
		}

		out->entries[i].title = strdup(AS_CSTRING(AS_TUPLE(entry)->values.values[0]));
		out->entries[i].action = strdup(AS_CSTRING(AS_TUPLE(entry)->values.values[1]));
	}

	out->entries[count].title = NULL;
	out->entries[count].action = NULL;

	out->set = menu_set_create();

	self->menuBar = out;
	out->_private = self;
	out->redraw_callback = _menubar_callback;

	krk_attachNamedValue(&self->inst.fields, "entries", OBJECT_VAL(entries));
	krk_attachNamedValue(&self->inst.fields, "set", krk_dict_of(0,NULL,0));

	return NONE_VAL();

_error:
	for (size_t i = 0; i < count; ++i) {
		if (out->entries[i].title) free(out->entries[i].title);
		if (out->entries[i].action) free(out->entries[i].action);
	}
	free(out->entries);
	free(out);

	return NONE_VAL();
}

KRK_Method(MenuBar,place) {
	int x, y;
	unsigned int width;
	struct _yutani_Window * window;

	if (!krk_parseArgs(
		".iiIO!", (const char*[]){"x","y","width","window"},
		&x, &y, &width,
		Window, &window)) {
		return NONE_VAL();
	}

	INIT_CHECK(MenuBar);

	self->menuBar->x = x;
	self->menuBar->y = y;
	self->menuBar->width = width;
	self->menuBar->window = window->window;

	return NONE_VAL();
}

KRK_Method(MenuBar,render) {
	struct _yutani_GraphicsContext * ctx;
	if (!krk_parseArgs(".O!",(const char*[]){"ctx"}, GraphicsContext, &ctx)) return NONE_VAL();
	INIT_CHECK(MenuBar);
	menu_bar_render(self->menuBar, ctx->ctx);
	return NONE_VAL();
}

KRK_Method(MenuBar,mouse_event) {
	struct _yutani_Window * window;
	KrkValue message;

	if (!krk_parseArgs(
		".O!V!", (const char*[]){"window", "message"},
		Window, &window,
		Message_WindowMouseEvent, &message
	)) {
		return NONE_VAL();
	}

	INIT_CHECK(MenuBar);
	return INTEGER_VAL(menu_bar_mouse_event(yctxInstance->yctx, window->window, self->menuBar,
		AS_Message_WindowMouseEvent(message),
		AS_Message_WindowMouseEvent(message)->new_x ,
		AS_Message_WindowMouseEvent(message)->new_y));
}

KRK_Method(MenuBar,insert) {
	KrkValue name;
	struct _yutani_MenuList * menu;

	if (!krk_parseArgs(
		".V!O!", (const char*[]){"name","menu"},
		KRK_BASE_CLASS(str), &name,
		MenuList, &menu
	)) {
		return NONE_VAL();
	}

	INIT_CHECK(MenuBar);
	menu_set_insert(self->menuBar->set, AS_CSTRING(name), menu->menuList);

	KrkValue dict = NONE_VAL();
	if (!krk_tableGet(&self->inst.fields, OBJECT_VAL(S("set")), &dict) || !krk_isInstanceOf(dict,KRK_BASE_CLASS(dict))) {
		return krk_runtimeError(vm.exceptions->typeError, "corrupt MenuBar");
	}

	krk_tableSet(AS_DICT(dict), name, OBJECT_VAL(menu));

	return NONE_VAL();
}

KRK_Method(MenuBar,height) {
	return INTEGER_VAL(MENU_BAR_HEIGHT);
}

#undef CURRENT_CTYPE

#define IS_MenuList(o) (krk_isInstanceOf(o,MenuList))
#define AS_MenuList(o) ((struct _yutani_MenuList*)AS_OBJECT(o))
#define CURRENT_CTYPE struct _yutani_MenuList*

KRK_Method(MenuList,__init__) {
	if (!krk_parseArgs(".:MenuList",(const char*[]){}, NULL)) return NONE_VAL();

	NO_REINIT(MenuList);

	struct MenuList * out = menu_create();
	self->menuList = out;

	KrkValue list = krk_list_of(0,NULL,0);
	krk_attachNamedValue(&self->inst.fields, "entries", list);

	return NONE_VAL();
}

KRK_Method(MenuList,insert) {
	struct _yutani_MenuEntry * entry;

	if (!krk_parseArgs(".O!",(const char*[]){"entry"}, MenuEntry, &entry)) return NONE_VAL();

	INIT_CHECK(MenuList);

	menu_insert(self->menuList, entry->menuEntry);

	KrkValue list = NONE_VAL();

	if (!krk_tableGet(&self->inst.fields, OBJECT_VAL(S("entries")), &list) || !IS_list(list)) {
		return krk_runtimeError(vm.exceptions->typeError, "corrupt MenuList");
	}

	krk_writeValueArray(AS_LIST(list), OBJECT_VAL(entry));

	return NONE_VAL();
}

#undef CURRENT_CTYPE

#define IS_MenuEntry(o) (krk_isInstanceOf(o,MenuEntry))
#define AS_MenuEntry(o) ((struct _yutani_MenuEntry*)AS_OBJECT(o))
#define CURRENT_CTYPE struct _yutani_MenuEntry*

static void _MenuEntry_callback_internal(struct MenuEntry * _self) {
	CURRENT_CTYPE self = (CURRENT_CTYPE)_self->_private;

	KrkValue callback = NONE_VAL();
	if (krk_tableGet(&self->inst.fields, OBJECT_VAL(S("callback")), &callback)) {
		krk_push(callback);
		krk_push(OBJECT_VAL(self));
		krk_callStack(1);
	}
}

KRK_Method(MenuEntry,__init__) {
	const char * title;
	KrkValue callback;
	const char * icon = NULL;
	const char * action = NULL;

	if (!krk_parseArgs(
		".sV|zz:MenuEntry", (const char*[]){"title","callback","icon","action"},
		&title, &callback,
		&icon, &action
	)) {
		return NONE_VAL();
	}

	NO_REINIT(MenuEntry);

	struct MenuEntry * out = menu_create_normal(icon, action, title, _MenuEntry_callback_internal);
	self->menuEntry = out;
	out->_private = self;

	krk_attachNamedValue(&self->inst.fields, "callback", callback);

	return NONE_VAL();
}

#define MENU_ENTRY_INT_PROP(name) \
	KRK_Method(MenuEntry,name) { \
		int set = 0, to = 0; \
		if (!krk_parseArgs(".|i?",(const char*[]){"value"},&set,&to)) return NONE_VAL(); \
		if (set) self->menuEntry-> name = to; \
		return INTEGER_VAL(self->menuEntry-> name); \
	}

MENU_ENTRY_INT_PROP(height)
MENU_ENTRY_INT_PROP(width)
MENU_ENTRY_INT_PROP(rwidth)
MENU_ENTRY_INT_PROP(hilight)
MENU_ENTRY_INT_PROP(offset)


KRK_Method(MenuEntry,update_icon) {
	char * icon;
	if (!krk_parseArgs(".z", (const char*[]){"icon"}, &icon)) return NONE_VAL();

	INIT_CHECK(MenuEntry);

	menu_update_icon(self->menuEntry, icon);

	return NONE_VAL();
}


#undef CURRENT_CTYPE

#define IS_MenuEntrySubmenu(o) (krk_isInstanceOf(o,MenuEntrySubmenu))
#define AS_MenuEntrySubmenu(o) ((struct _yutani_MenuEntrySubmenu*)AS_OBJECT(o))
#define CURRENT_CTYPE struct _yutani_MenuEntrySubmenu*

KRK_Method(MenuEntrySubmenu,__init__) {
	const char * title;
	const char * action = NULL;
	const char * icon = NULL;

	if (!krk_parseArgs(
		".ss|z:MenuEntrySubmenu", (const char*[]){"title","action","icon"},
		&title,
		&action,
		&icon
	)) {
		return NONE_VAL();
	}

	NO_REINIT(MenuEntrySubmenu);

	struct MenuEntry * out = menu_create_submenu(icon, action, title);
	self->menuEntry = out;
	out->_private = self;

	return NONE_VAL();
}

#undef CURRENT_CTYPE

#define IS_MenuEntrySeparator(o) (krk_isInstanceOf(o,MenuEntrySeparator))
#define AS_MenuEntrySeparator(o) ((struct _yutani_MenuEntrySeparator*)AS_OBJECT(o))
#define CURRENT_CTYPE struct _yutani_MenuEntrySeparator*

KRK_Method(MenuEntrySeparator,__init__) {
	if (!krk_parseArgs(".:MenuEntrySeparator", (const char*[]){}, NULL)) return NONE_VAL();
	NO_REINIT(MenuEntrySeparator);
	struct MenuEntry * out = menu_create_separator();
	self->menuEntry = out;
	out->_private = self;
	return NONE_VAL();
}

#undef CURRENT_CTYPE

#define IS_MenuEntryCustom(o) (krk_isInstanceOf(o,MenuEntryCustom))
#define AS_MenuEntryCustom(o) ((struct _yutani_MenuEntryCustom*)AS_OBJECT(o))
#define CURRENT_CTYPE struct _yutani_MenuEntryCustom*

static void _custom_menu_render(gfx_context_t * ctx, struct MenuEntry  * _self, int offset) {
	CURRENT_CTYPE self = (CURRENT_CTYPE)_self->_private;
	KrkClass * myClass = self->inst._class;
	KrkValue method;
	if (!krk_tableGet_fast(&myClass->methods, S("render"), &method)) return;
	krk_push(method);
	krk_push(OBJECT_VAL(self));

	struct _yutani_GraphicsContext * gctx = (struct _yutani_GraphicsContext*)krk_newInstance(GraphicsContext);
	gctx->ctx = ctx;
	krk_push(OBJECT_VAL(gctx));
	krk_push(INTEGER_VAL(offset));

	krk_callStack(3);
}

static void _custom_menu_focus_change(struct MenuEntry * _self, int focused) {
	CURRENT_CTYPE self = (CURRENT_CTYPE)_self->_private;
	KrkClass * myClass = self->inst._class;
	KrkValue method;
	if (!krk_tableGet_fast(&myClass->methods, S("focus_change"), &method)) return;
	krk_push(method);
	krk_push(OBJECT_VAL(self));
	krk_push(BOOLEAN_VAL(focused));
	krk_callStack(2);
}

static void _custom_menu_activate(struct MenuEntry * _self, int focused) {
	CURRENT_CTYPE self = (CURRENT_CTYPE)_self->_private;
	KrkClass * myClass = self->inst._class;
	KrkValue method;
	if (!krk_tableGet_fast(&myClass->methods, S("activate"), &method)) return;
	krk_push(method);
	krk_push(OBJECT_VAL(self));
	krk_push(BOOLEAN_VAL(focused));
	krk_callStack(2);
}

static int _custom_menu_mouse_event(struct MenuEntry * _self, struct yutani_msg_window_mouse_event * event) {
	CURRENT_CTYPE self = (CURRENT_CTYPE)_self->_private;
	KrkClass * myClass = self->inst._class;
	KrkValue method;
	if (!krk_tableGet_fast(&myClass->methods, S("mouse_event"), &method)) return 0;
	krk_push(method);
	krk_push(OBJECT_VAL(self));

	size_t size = sizeof(yutani_msg_t) + sizeof(struct yutani_msg_window_mouse_event);
	yutani_msg_t * tmp = malloc(size);
	tmp->type = YUTANI_MSG_WINDOW_MOUSE_EVENT;
	tmp->size = size;
	memcpy(tmp->data, event, sizeof(struct yutani_msg_window_mouse_event));

	krk_push(makeMessage(tmp));

	KrkValue result = krk_callStack(2);
	if (IS_INTEGER(result)) return AS_INTEGER(result);
	return 0;
}

static struct MenuEntryVTable _custom_menu_vtable = {
	.methods = 4,
	.renderer = _custom_menu_render,
	.focus_change = _custom_menu_focus_change,
	.activate = _custom_menu_activate,
	.mouse_event = _custom_menu_mouse_event,
};

KRK_Method(MenuEntryCustom,__init__) {
	if (!krk_parseArgs(".:MenuEntryCustom", (const char*[]){}, NULL)) return NONE_VAL();
	NO_REINIT(MenuEntryCustom);
	struct MenuEntry * out = menu_create_separator(); /* Steal some defaults */
	out->_type = -1; /* Special */
	out->vtable = &_custom_menu_vtable;
	self->menuEntry = out;
	out->_private = self;
	return NONE_VAL();
}

#undef CURRENT_CTYPE

#define CURRENT_CTYPE struct _yutani_TTContour*

void _TTContour_ongcsweep(KrkInstance * _self) {
	CURRENT_CTYPE self = (CURRENT_CTYPE)_self;
	if (self->contour) {
		free(self->contour);
	}
	self->contour = NULL;
}

KRK_Method(TTContour,__init__) {
	float x, y;
	if (!krk_parseArgs(".ff:TTContour", (const char*[]){"x","y"}, &x, &y))
		return NONE_VAL();

	NO_REINIT(TTContour);
	self->contour = tt_contour_start(x,y);

	return NONE_VAL();
}

KRK_Method(TTContour,line_to) {
	float x, y;
	if (!krk_parseArgs(".ff", (const char*[]){"x","y"}, &x, &y))
		return NONE_VAL();
	INIT_CHECK(TTContour);
	self->contour = tt_contour_line_to(self->contour, x, y);
	return NONE_VAL();
}

KRK_Method(TTContour,move_to) {
	float x, y;
	if (!krk_parseArgs(".ff", (const char*[]){"x","y"}, &x, &y))
		return NONE_VAL();
	INIT_CHECK(TTContour);
	self->contour = tt_contour_move_to(self->contour, x, y);
	return NONE_VAL();
}

KRK_Method(TTContour,finish) {
	INIT_CHECK(TTContour);
	struct _yutani_TTShape * newShape = (struct _yutani_TTShape*)krk_newInstance(TTShape);
	newShape->shape = tt_contour_finish(self->contour);
	return OBJECT_VAL(newShape);
}

KRK_Method(TTContour,stroke) {
	float width;
	if (!krk_parseArgs(".f", (const char*[]){"width"}, &width)) return NONE_VAL();
	INIT_CHECK(TTContour);
	struct _yutani_TTShape * newShape = (struct _yutani_TTShape*)krk_newInstance(TTShape);
	newShape->shape = tt_contour_stroke_shape(self->contour, width);
	return OBJECT_VAL(newShape);
}

KRK_Method(TTContour,stroke_path) {
	float width;
	if (!krk_parseArgs(".f", (const char*[]){"width"}, &width)) return NONE_VAL();
	INIT_CHECK(TTContour);
	struct _yutani_TTContour * newContour = (struct _yutani_TTContour*)krk_newInstance(TTContour);
	newContour->contour = tt_contour_stroke_contour(self->contour, width);
	return OBJECT_VAL(newContour);
}

KRK_Method(TTContour,free) {
	INIT_CHECK(TTContour);
	free(self->contour);
	self->contour = NULL;
	return NONE_VAL();
}

KRK_Method(TTContour,transform) {
	struct _yutani_TransformMatrix * matrix;
	if (!krk_parseArgs(".O!", (const char*[]){"matrix"},
		TransformMatrix, &matrix)) return NONE_VAL();
	INIT_CHECK(TTContour);

	tt_contour_transform(self->contour, matrix->matrix);

	return NONE_VAL();
}

#undef CURRENT_CTYPE
#define CURRENT_CTYPE struct _yutani_TTShape*

void _TTShape_ongcsweep(KrkInstance * _self) {
	CURRENT_CTYPE self = (CURRENT_CTYPE)_self;
	if (self->shape) {
		fprintf(stderr, "free shape\n");
		free(self->shape);
	}
	self->shape = NULL;
}

KRK_Method(TTShape,__init__) {
	return krk_runtimeError(vm.exceptions->typeError, "Can not initialize empty shape; use TTContour.finish instead");
}

KRK_Method(TTShape,paint) {
	struct _yutani_GraphicsContext * ctx;
	uint32_t color;

	if (!krk_parseArgs(
		".O!I", (const char*[]){"ctx","color"},
		GraphicsContext, &ctx, &color)) {
		return NONE_VAL();
	}

	INIT_CHECK(TTShape);

	tt_path_paint(ctx->ctx, self->shape, color);

	return NONE_VAL();
}

extern void tt_path_paint_sprite(gfx_context_t * ctx, const struct TT_Shape * shape, sprite_t * sprite, gfx_matrix_t matrix);
extern void tt_path_paint_sprite_options(gfx_context_t * ctx, const struct TT_Shape * shape, sprite_t * sprite, gfx_matrix_t matrix, int, int);
KRK_Method(TTShape,paint_sprite) {
	struct _yutani_GraphicsContext * ctx;
	struct _yutani_Sprite * sprite;
	struct _yutani_TransformMatrix * matrix;

	int filter = 0;
	int wrap = 0;

	if (!krk_parseArgs(
		".O!O!O!|ii", (const char*[]){"ctx","sprite","matrix","filter","wrap"},
		GraphicsContext, &ctx,
		Sprite, &sprite,
		TransformMatrix, &matrix,
		&filter, &wrap)) {
		return NONE_VAL();
	}

	INIT_CHECK(TTShape);
	if (!sprite->sprite) return krk_runtimeError(vm.exceptions->valueError, "sprite go brrr");

	if (filter == 0 && wrap == 0) {
		tt_path_paint_sprite(ctx->ctx, self->shape, sprite->sprite, matrix->matrix);
	} else {
		tt_path_paint_sprite_options(ctx->ctx, self->shape, sprite->sprite, matrix->matrix, filter, wrap);
	}

	return NONE_VAL();
}

KRK_Method(TTShape,free) {
	INIT_CHECK(TTShape);
	free(self->shape);
	self->shape = NULL;
	return NONE_VAL();
}

#undef CURRENT_CTYPE

KRK_Function(decor_get_bounds) {
	struct _yutani_Window * window = NULL;
	if (!krk_parseArgs("|O!",(const char *[]){"window"}, Window, &window)) return NONE_VAL();
	if (window && !window->window) return krk_runtimeError(vm.exceptions->valueError, "Window is closed");
	struct decor_bounds bounds;
	decor_get_bounds(window ? window->window : NULL, &bounds);

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

KRK_Function(decor_render) {
	struct _yutani_Window * window;
	const char * title = NULL;
	if (!krk_parseArgs("O!|z",(const char *[]){"window","title"}, Window, &window, &title)) return NONE_VAL();
	if (!window->window) return krk_runtimeError(vm.exceptions->valueError, "Window is closed");
	if (!title) title = IS_NONE(window->title) ? "" : AS_CSTRING(window->title);
	render_decorations(window->window, window->ctx, (char*)title);
	return NONE_VAL();
}

KRK_Function(decor_handle_event) {
	struct _yutani_Message * message = NULL;
	if (!krk_parseArgs("O!",(const char *[]){"message"}, Message, &message)) return NONE_VAL();
	return INTEGER_VAL(decor_handle_event(yctxInstance->yctx, message->msg));
}

KRK_Function(decor_show_default_menu) {
	struct _yutani_Window * window;
	int x, y;
	if (!krk_parseArgs("O!ii",(const char *[]){"window","x","y"}, Window, &window, &x, &y)) return NONE_VAL();
	if (!window->window) return krk_runtimeError(vm.exceptions->valueError, "Window is closed");
	decor_show_default_menu(window->window, x, y);
	return NONE_VAL();
}

KRK_Function(rgb) {
	int r, g, b;
	KrkValue a = NONE_VAL();
	if (!krk_parseArgs("bbb|V",(const char*[]){"r","g","b","a"}, &r, &g, &b, &a)) return NONE_VAL();
	if (IS_NONE(a)) {
		return INTEGER_VAL(rgb(r,g,b));
	} else {
		if (IS_FLOATING(a)) a = INTEGER_VAL(AS_FLOATING(a) * 255);
		if (!IS_INTEGER(a)) return TYPE_ERROR(int or float,a);
		return INTEGER_VAL(rgba(r,g,b,AS_INTEGER(a)));
	}
}

KRK_Function(draw_button) {
	struct _yutani_GraphicsContext * ctx;
	int x, y, width, height, hilight;
	const char * title;

	if (!krk_parseArgs("O!iiIIsi",
		(const char*[]){"ctx","x","y","width","height","title","hilight"},
		GraphicsContext, &ctx,
		&x, &y, &width, &height,
		&title, &hilight)) {
		return NONE_VAL();
	}

	struct TTKButton button = {x,y,width,height,(char*)title,hilight};
	ttk_button_draw(ctx->ctx, &button);
	return NONE_VAL();
}

KRK_Function(fswait) {
	KrkTuple * fds;
	int timeout = -1;

	if (!krk_parseArgs("O!|i",(const char*[]){"fds","timeout"},
		KRK_BASE_CLASS(tuple), &fds,
		&timeout)) {
		return NONE_VAL();
	}

	size_t count = fds->values.count;

	if (!count) {
		return krk_runtimeError(vm.exceptions->typeError, "can not wait on nothing?");
	}

	/* Spot check first */
	for (size_t i = 0; i < count; ++i) {
		KrkValue val = fds->values.values[i];
		if (!IS_INTEGER(val)) return krk_runtimeError(vm.exceptions->typeError, "fds must be tuple of int, not %T", val);
	}

	int * _fds = malloc(sizeof(int) * count);
	int * _results = malloc(sizeof(int) * count);
	for (size_t i = 0; i < count; ++i) {
		KrkValue val = fds->values.values[i];
		_fds[i] = AS_INTEGER(val);
		_results[i] = 0;
	}

	errno = 0;
	int status = fswait3(count, _fds, timeout, _results);
	free(_fds);

	if (status < 0) {
		int _errno = errno;
		free(_results);
		/* check if we were already raising a keyboard interrupt */
		if (krk_currentThread.flags & (KRK_THREAD_HAS_EXCEPTION | KRK_THREAD_SIGNALLED)) return NONE_VAL();
		return krk_runtimeError(vm.exceptions->OSError, "%s", strerror(_errno));
	}

	KrkTuple * output = krk_newTuple(count);
	krk_push(OBJECT_VAL(output));
	for (size_t i = 0; i < count; ++i) {
		output->values.values[output->values.count++] = INTEGER_VAL(_results[i]);
	}

	free(_results);

	return krk_pop();
}

#undef CURRENT_CTYPE

KrkValue krk_module_onload__yutani2(void) {
	module = krk_newInstance(KRK_BASE_CLASS(module));
	krk_push(OBJECT_VAL(module));

	/**
	 * Base message type
	 */
	krk_makeClass(module, &Message, "Message", KRK_BASE_CLASS(object));
	Message->allocSize = sizeof(struct _yutani_Message);
	Message->_ongcsweep = _Message_gcsweep;
	BIND_STATICMETHOD(Message,__new__);
	BIND_METHOD(Message,__repr__);
	BIND_PROP(Message,msg_magic);
	BIND_PROP(Message,msg_type);
	BIND_PROP(Message,msg_size);
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
	krk_finalizeClass(Message);

#define MAKE_MSG(type) \
	krk_makeClass(module, &Message_ ## type, "Message_" # type, Message)

	MAKE_MSG(Welcome);
	BIND_PROP(Message_Welcome,display_width);
	BIND_PROP(Message_Welcome,display_height);
	krk_finalizeClass(Message_Welcome);

	MAKE_MSG(WindowMouseEvent);
	BIND_PROP(Message_WindowMouseEvent,wid);
	BIND_PROP(Message_WindowMouseEvent,new_x);
	BIND_PROP(Message_WindowMouseEvent,new_y);
	BIND_PROP(Message_WindowMouseEvent,old_x);
	BIND_PROP(Message_WindowMouseEvent,old_y);
	BIND_PROP(Message_WindowMouseEvent,buttons);
	BIND_PROP(Message_WindowMouseEvent,command);
	BIND_PROP(Message_WindowMouseEvent,modifiers);
	krk_finalizeClass(Message_WindowMouseEvent);

	MAKE_MSG(WindowFocusChange);
	BIND_PROP(Message_WindowFocusChange,wid);
	BIND_PROP(Message_WindowFocusChange,focused);
	krk_finalizeClass(Message_WindowFocusChange);

	MAKE_MSG(ResizeOffer);
	BIND_PROP(Message_ResizeOffer,wid);
	BIND_PROP(Message_ResizeOffer,width);
	BIND_PROP(Message_ResizeOffer,height);
	BIND_PROP(Message_ResizeOffer,bufid);
	krk_finalizeClass(Message_ResizeOffer);

	MAKE_MSG(WindowAdvertise);
	BIND_PROP(Message_WindowAdvertise,wid);
	BIND_PROP(Message_WindowAdvertise,flags);
	BIND_PROP(Message_WindowAdvertise,size);
	BIND_PROP(Message_WindowAdvertise,width);
	BIND_PROP(Message_WindowAdvertise,height);
	BIND_PROP(Message_WindowAdvertise,bufid);
	BIND_PROP(Message_WindowAdvertise,name);
	BIND_PROP(Message_WindowAdvertise,icon);
	krk_finalizeClass(Message_WindowAdvertise);

	MAKE_MSG(WindowMove);
	BIND_PROP(Message_WindowMove,wid);
	BIND_PROP(Message_WindowMove,x);
	BIND_PROP(Message_WindowMove,y);
	krk_finalizeClass(Message_WindowMove);

	MAKE_MSG(KeyEvent);
	BIND_PROP(Message_KeyEvent,wid);
	BIND_PROP(Message_KeyEvent,keycode);
	BIND_PROP(Message_KeyEvent,modifiers);
	BIND_PROP(Message_KeyEvent,action);
	BIND_PROP(Message_KeyEvent,key);
	BIND_PROP(Message_KeyEvent,kbd_state);
	BIND_PROP(Message_KeyEvent,kbd_s_state);
	BIND_PROP(Message_KeyEvent,k_ctrl);
	BIND_PROP(Message_KeyEvent,k_shift);
	BIND_PROP(Message_KeyEvent,k_alt);
	BIND_PROP(Message_KeyEvent,k_super);
	BIND_PROP(Message_KeyEvent,kl_ctrl);
	BIND_PROP(Message_KeyEvent,kl_shift);
	BIND_PROP(Message_KeyEvent,kl_alt);
	BIND_PROP(Message_KeyEvent,kl_super);
	BIND_PROP(Message_KeyEvent,kr_ctrl);
	BIND_PROP(Message_KeyEvent,kr_shift);
	BIND_PROP(Message_KeyEvent,kr_alt);
	BIND_PROP(Message_KeyEvent,kr_super);
	BIND_PROP(Message_KeyEvent,kbd_esc_buf);
	krk_finalizeClass(Message_KeyEvent);

	MAKE_MSG(WindowClose);
	BIND_PROP(Message_WindowClose,wid);
	krk_finalizeClass(Message_WindowClose);

	/**
	 * Core connection type; singleton
	 *
	 * class YutaniCtx:
	 *     display_width: int
	 *     display_height: int
	 *     def __new__(cls)
	 *     def poll(self, sync=True) -> Message
	 *     def wait_for(self, msgtype: int) -> Message
	 *     def subscribe(self)
	 *     def unsubscribe(self)
	 *     def query_windows(self)
	 *     def fileno(self) -> int
	 *     def query(self) -> int
	 *     def menu_process_event(self, message: Message) -> int
	 */
	krk_makeClass(module, &YutaniCtx, "YutaniCtx", KRK_BASE_CLASS(object));
	YutaniCtx->allocSize = sizeof(struct _yutani_YutaniCtx);
	YutaniCtx->obj.flags |= KRK_OBJ_FLAGS_NO_INHERIT;
	BIND_STATICMETHOD(YutaniCtx,__new__);
	BIND_METHOD(YutaniCtx,poll);
	BIND_METHOD(YutaniCtx,wait_for);
	BIND_METHOD(YutaniCtx,subscribe);
	BIND_METHOD(YutaniCtx,unsubscribe);
	BIND_METHOD(YutaniCtx,query_windows);
	BIND_METHOD(YutaniCtx,fileno);
	BIND_METHOD(YutaniCtx,query);
	BIND_METHOD(YutaniCtx,menu_process_event);
	BIND_PROP(YutaniCtx,display_width);
	BIND_PROP(YutaniCtx,display_height);
	krk_finalizeClass(YutaniCtx);

	/*
	 * Generic graphics context.
	 *    Subclassed by Window and Sprite.
	 *
	 * class GraphicsContext:
	 *     width: int
	 *     height: int
	 *     isDoubleBuffered: bool
	 *     def fill(self, color: int)
	 *     def flip(self)
	 *     def blur(self, radius: int = 2)
	 *     def line(self, x0: int, x1: int, y0: int, y1: int, color: int, thickness=None)
	 *     def rect(self, x: int, y: int, width: int, height: int, color: int, solid: bool = False, radius: int = 0)
	 *     def draw_sprite(self, sprite: Sprite, x: int, y: int, alpha: float = 1.0, rotation: float = 0.0, scale: tuple[int,int] = None, color: int = 0)
	 *
	 * To allocate a new graphics context with a fresh backing store, use Sprite.
	 */
	krk_makeClass(module, &GraphicsContext, "GraphicsContext", KRK_BASE_CLASS(object));
	GraphicsContext->allocSize = sizeof(struct _yutani_GraphicsContext);
	GraphicsContext->obj.flags |= KRK_OBJ_FLAGS_NO_INHERIT;
	BIND_STATICMETHOD(GraphicsContext,__new__);
	BIND_PROP(GraphicsContext,width);
	BIND_PROP(GraphicsContext,height);
	BIND_PROP(GraphicsContext,isDoubleBuffered);
	BIND_METHOD(GraphicsContext,fill);
	BIND_METHOD(GraphicsContext,flip);
	BIND_METHOD(GraphicsContext,blur);
	BIND_METHOD(GraphicsContext,line);
	BIND_METHOD(GraphicsContext,rect);
	BIND_METHOD(GraphicsContext,draw_sprite);
	krk_finalizeClass(GraphicsContext);

	/*
	 * Graphics object with a bitmap backing store,
	 * typically derived from an image file.
	 *
	 * class Sprite(GraphicsContext):
	 *     def __init__(self, file=None, width=0, height=0)
	 */
	krk_makeClass(module, &Sprite, "Sprite", GraphicsContext);
	Sprite->allocSize = sizeof(struct _yutani_Sprite);
	Sprite->_ongcsweep = _yutani_Sprite_gcsweep;
	BIND_METHOD(Sprite,__init__);
	BIND_METHOD(Sprite,__repr__);
	BIND_METHOD(Sprite,free);
	krk_finalizeClass(Sprite);

	/*
	 * A window.
	 *
	 * class Window(GraphicsContext):
	 *     title: str
	 *     icon: str
	 *     wid: int
	 *     x: int
	 *     y: int
	 *     focused: bool
	 *     closed: bool
	 *     def __init__(self, width: int, height: int, flags: int = 0, title: str = None, icon: str = None, doublebuffer: bool = True)
	 *     def flip(self)
	 *     def move(self, x: int, y: int)
	 *     def close(self)
	 *     def set_stack(self, z: int)
	 *     def special_request(self: request: int)
	 *     def resize(self, width: int, height: int)
	 *     def resize_start(self, direction: int)
	 *     def resize_done(self)
	 *     def resize_offer(self, width: int, height: int)
	 *     def resize_accept(self, width: int, height: int)
	 *     def update_shape(self, threshold: int)
	 *     def show_mouse(self, mouse: int)
	 *     def warp_mouse(self, x: int, y: int)
	 *     def reinit(self)
	 */
	krk_makeClass(module, &Window, "Window", GraphicsContext);
	Window->allocSize = sizeof(struct _yutani_Window);
	Window->_ongcscan = _yutani_Window_gcscan;
	BIND_METHOD(Window,__init__);
	BIND_METHOD(Window,__repr__);
	BIND_METHOD(Window,flip);
	BIND_METHOD(Window,move);
	BIND_METHOD(Window,close);
	BIND_METHOD(Window,set_stack);
	BIND_METHOD(Window,special_request);
	BIND_METHOD(Window,resize);
	BIND_METHOD(Window,resize_start);
	BIND_METHOD(Window,resize_done);
	BIND_METHOD(Window,resize_offer);
	BIND_METHOD(Window,resize_accept);
	BIND_METHOD(Window,update_shape);
	BIND_METHOD(Window,show_mouse);
	BIND_METHOD(Window,warp_mouse);
	BIND_METHOD(Window,reinit);

	BIND_PROP(Window,title);
	BIND_PROP(Window,icon);
	BIND_PROP(Window,wid);
	BIND_PROP(Window,x);
	BIND_PROP(Window,y);
	BIND_PROP(Window,focused);
	BIND_PROP(Window,closed);
	krk_finalizeClass(Window);

	krk_makeClass(module, &Subregion, "Subregion", GraphicsContext);
	Subregion->allocSize = sizeof(struct _yutani_Subregion);
	Subregion->_ongcsweep = _yutani_Subregion_gcsweep;
	BIND_METHOD(Subregion,__init__);
	BIND_PROP(Subregion,offset_x);
	BIND_PROP(Subregion,offset_y);
	krk_finalizeClass(Subregion);

	/*
	 * Typeface using the 'text' library.
	 *
	 * class Font:
	 *     size: int
	 *     def __init__(self, font: str, size: int, color: int = rgb(0,0,0))
	 *     def draw_string(self, ctx: GraphicsContext, s: str, x: int, y: int) -> int
	 *     def draw_string_shadow(self, ctx: GraphicsContext, s: str, x: int, y: int, shadow: int, blur: int)
	 *     def width(self, s: str) -> int
	 */
	krk_makeClass(module, &Font, "Font", KRK_BASE_CLASS(object));
	Font->allocSize = sizeof(struct _yutani_Font);
	Font->_ongcsweep = _yutani_Font_gcsweep;
	BIND_METHOD(Font,__init__);
	BIND_METHOD(Font,draw_string);
	BIND_METHOD(Font,draw_string_shadow);
	BIND_METHOD(Font,width);
	BIND_METHOD(Font,measure);
	BIND_METHOD(Font,draw_glyph_into);
	BIND_METHOD(Font,prepare_string);
	BIND_METHOD(Font,ellipsify);
	BIND_PROP(Font,size);
	krk_finalizeClass(Font);

	/*
	 * Menu bar widget.
	 *
	 * This should really be in a higher-level GUI toolkit, but for now we have what we have...
	 *
	 * class MenuBar:
	 *     def __init__(self, entries: tuple[tuple[str,str]])
	 *     def place(self, x: int, y: int, width: int, window: Window)
	 *     def render(self, ctx: GraphicsContext)
	 *     def mouse_event(self, window: Window, message: Message_WindowMouseEvent)
	 *     def insert(self, name: str, menu: MenuList)
	 */
	krk_makeClass(module, &MenuBar, "MenuBar", KRK_BASE_CLASS(object));
	MenuBar->allocSize = sizeof(struct _yutani_MenuBar);
	MenuBar->_ongcsweep = _yutani_MenuBar_gcsweep;
	BIND_METHOD(MenuBar,__init__);
	BIND_METHOD(MenuBar,place);
	BIND_METHOD(MenuBar,render);
	BIND_METHOD(MenuBar,mouse_event);
	BIND_METHOD(MenuBar,insert);
	BIND_PROP(MenuBar,height);

	krk_finalizeClass(MenuBar);

	/*
	 * MenuList wrapper
	 *
	 * class MenuList:
	 *     def __init__(self)
	 *     def insert(self, entry: MenuEntry)
	 */
	krk_makeClass(module, &MenuList, "MenuList", KRK_BASE_CLASS(object));
	MenuList->allocSize = sizeof(struct _yutani_MenuList);
	/* XXX where is the cleanup function for this? */
	BIND_METHOD(MenuList,__init__);
	BIND_METHOD(MenuList,insert);
	krk_finalizeClass(MenuList);


	/*
	 * Menu entry wrapper.
	 *
	 * class MenuEntry:
	 *     def __init__(self, title: str, callback: function, icon: str = None, action: str = None)
	 */
	krk_makeClass(module, &MenuEntry, "MenuEntry", KRK_BASE_CLASS(object));
	MenuEntry->allocSize = sizeof(struct _yutani_MenuEntry);
	BIND_METHOD(MenuEntry,__init__);
	BIND_PROP(MenuEntry,height);
	BIND_PROP(MenuEntry,width);
	BIND_PROP(MenuEntry,rwidth);
	BIND_PROP(MenuEntry,hilight);
	BIND_PROP(MenuEntry,offset);
	BIND_METHOD(MenuEntry,update_icon);
	krk_finalizeClass(MenuEntry);

	/*
	 * Submenu subtype
	 *
	 * class MenuEntrySubmenu(MenuEntry):
	 *     def __init__(self, title: str, icon: str = None, action: str = None)
	 */
	krk_makeClass(module, &MenuEntrySubmenu, "MenuEntrySubmenu", MenuEntry);
	MenuEntrySubmenu->allocSize = sizeof(struct _yutani_MenuEntrySubmenu);
	BIND_METHOD(MenuEntrySubmenu,__init__);
	krk_finalizeClass(MenuEntrySubmenu);

	/**
	 * Separator subtype
	 *
	 * class MenuEntrySeparator(MenuEntry):
	 *     def __init__(self)
	 */
	krk_makeClass(module, &MenuEntrySeparator, "MenuEntrySeparator", MenuEntry);
	MenuEntrySeparator->allocSize = sizeof(struct _yutani_MenuEntrySeparator);
	BIND_METHOD(MenuEntrySeparator,__init__);
	krk_finalizeClass(MenuEntrySeparator);

	krk_makeClass(module, &MenuEntryCustom, "MenuEntryCustom", MenuEntry);
	MenuEntryCustom->allocSize = sizeof(struct _yutani_MenuEntryCustom);
	BIND_METHOD(MenuEntryCustom,__init__);
	krk_finalizeClass(MenuEntryCustom);


	krk_makeClass(module, &TTContour, "TTContour", KRK_BASE_CLASS(object));
	TTContour->allocSize = sizeof(struct _yutani_TTContour);
	TTContour->_ongcsweep = _TTContour_ongcsweep;
	BIND_METHOD(TTContour,__init__);
	BIND_METHOD(TTContour,line_to);
	BIND_METHOD(TTContour,move_to);
	BIND_METHOD(TTContour,finish);
	BIND_METHOD(TTContour,free);
	BIND_METHOD(TTContour,stroke);
	BIND_METHOD(TTContour,stroke_path);
	BIND_METHOD(TTContour,transform);
	krk_finalizeClass(TTContour);

	krk_makeClass(module, &TTShape, "TTShape", KRK_BASE_CLASS(object));
	TTShape->allocSize = sizeof(struct _yutani_TTShape);
	TTShape->_ongcsweep = _TTShape_ongcsweep;
	BIND_METHOD(TTShape,__init__);
	BIND_METHOD(TTShape,paint);
	BIND_METHOD(TTShape,paint_sprite);
	BIND_METHOD(TTShape,free);
#define CONST(n) krk_attachNamedValue(&TTShape->methods, #n, INTEGER_VAL(n))
	CONST(TT_PATH_FILTER_BILINEAR);
	CONST(TT_PATH_FILTER_NEAREST);
	CONST(TT_PATH_WRAP_REPEAT);
	CONST(TT_PATH_WRAP_NONE);
	CONST(TT_PATH_WRAP_PAD);
#undef CONST
	krk_finalizeClass(TTShape);

	krk_makeClass(module, &TransformMatrix, "TransformMatrix", KRK_BASE_CLASS(object));
	TransformMatrix->allocSize = sizeof(struct _yutani_TransformMatrix);
	BIND_METHOD(TransformMatrix,__init__);
	BIND_METHOD(TransformMatrix,__repr__);
	BIND_METHOD(TransformMatrix,scale);
	BIND_METHOD(TransformMatrix,translate);
	BIND_METHOD(TransformMatrix,rotate);
	BIND_METHOD(TransformMatrix,shear);
	BIND_METHOD(TransformMatrix,apply);
	BIND_PROP(TransformMatrix,a);
	BIND_PROP(TransformMatrix,b);
	BIND_PROP(TransformMatrix,tx);
	BIND_PROP(TransformMatrix,c);
	BIND_PROP(TransformMatrix,d);
	BIND_PROP(TransformMatrix,ty);
	krk_finalizeClass(TransformMatrix);

	BIND_FUNC(module,decor_get_bounds);
	BIND_FUNC(module,decor_render);
	BIND_FUNC(module,decor_handle_event);
	BIND_FUNC(module,decor_show_default_menu);

	BIND_FUNC(module,rgb);

	BIND_FUNC(module,draw_button);
	BIND_FUNC(module,fswait);

	return krk_pop(); /* module */
}
