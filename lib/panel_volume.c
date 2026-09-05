/**
 * @brief Panel Volume Widget
 *
 * Shows an icon indicating the mixer's master volume,
 * and shows a menu with a volume slider when clicked.
 */
#include <fcntl.h>
#include <sys/ioctl.h>
#include <kernel/mod/sound.h>

#include <toaru/yutani.h>
#include <toaru/graphics.h>
#include <toaru/menu.h>
#include <toaru/text.h>
#include <toaru/panel.h>

#define VOLUME_DEVICE_ID 0
#define VOLUME_KNOB_ID   0

static sprite_t * sprite_volume_mute;
static sprite_t * sprite_volume_low;
static sprite_t * sprite_volume_med;
static sprite_t * sprite_volume_high;
static struct MenuList * volume_menu;
static struct MenuEntry_Slider * volume_slider;

static long volume_level = 0;
static int mixer = -1;

static int widget_update_volume(struct PanelWidget * this, int * force_updates) {
	if (mixer == -1) {
		mixer = open("/dev/mixer", O_RDONLY | O_CLOEXEC);
	}

	snd_knob_value_t value = {0};
	value.device = VOLUME_DEVICE_ID; /* TODO configure this somewhere */
	value.id     = VOLUME_KNOB_ID;   /* TODO this too */

	ioctl(mixer, SND_MIXER_READ_KNOB, &value);
	volume_level = value.val;

	return 0;
}

static void set_volume(void) {
	snd_knob_value_t value = {0};
	value.device = VOLUME_DEVICE_ID; /* TODO configure this somewhere */
	value.id     = VOLUME_KNOB_ID;   /* TODO this too */
	value.val    = volume_level;

	ioctl(mixer, SND_MIXER_WRITE_KNOB, &value);

	volume_slider->value = (float)volume_level / (float)0xFC000000;

	redraw();
}

static void volume_raise(void) {
	volume_level += 0x10000000;
	if (volume_level > 0xF0000000) volume_level = 0xFC000000;
	set_volume();
}

static void volume_lower(void) {
	volume_level -= 0x10000000;
	if (volume_level < 0x0) volume_level = 0x0;
	set_volume();
}

static void volume_slider_callback(struct MenuEntry *_self) {
	struct MenuEntry_Slider * self = (void *)_self;
	volume_level = self->value * 0xFC000000;
	set_volume();
}

static int widget_click_volume(struct PanelWidget * this, struct yutani_msg_window_mouse_event * evt) {
	if (!volume_menu) {
		volume_menu = menu_create();
		volume_menu->flags |= MENU_FLAG_BUBBLE_LEFT;
	}

	/* Clear the menu */
	while (volume_menu->entries->length) {
		node_t * node = list_pop(volume_menu->entries);
		menu_free_entry((struct MenuEntry *)node->value);
		free(node);
	}

	volume_slider = (struct MenuEntry_Slider*)menu_create_slider(sprite_volume_high, (float)volume_level / (float)0xFC000000, volume_slider_callback);

	menu_insert(volume_menu, (struct MenuEntry*)volume_slider);

	/* TODO Our mixer supports multiple knobs and we could show all of them. */
	/* TODO We could also show a nice slider... if we had one... */

	if (!volume_menu->window) {
		panel_menu_show(this, volume_menu);
	}

	return 1;
}


static int widget_draw_volume(struct PanelWidget * this, gfx_context_t * ctx) {
	uint32_t color = (volume_menu && volume_menu->window) ? this->pctx->color_text_hilighted : this->pctx->color_icon_normal;

	panel_highlight_widget(this,ctx,(volume_menu && volume_menu->window));

	if (volume_level < 10) {
		draw_sprite_alpha_paint(ctx, sprite_volume_mute, (ctx->width - sprite_volume_mute->width) / 2, 1, 1.0, color);
	} else if (volume_level < 0x547ae147) {
		draw_sprite_alpha_paint(ctx, sprite_volume_low,  (ctx->width - sprite_volume_low->width) / 2, 1, 1.0, color);
	} else if (volume_level < 0xa8f5c28e) {
		draw_sprite_alpha_paint(ctx, sprite_volume_med,  (ctx->width - sprite_volume_med->width) / 2, 1, 1.0, color);
	} else {
		draw_sprite_alpha_paint(ctx, sprite_volume_high, (ctx->width - sprite_volume_high->width) / 2, 1, 1.0, color);
	}

	return 0;
}

/* For dumb legacy reasons, scroll wheel movement shows up here... */
static int widget_move_volume(struct PanelWidget * this, struct yutani_msg_window_mouse_event * evt) {
	int scroll_direction = 0;
	if (evt->buttons & YUTANI_MOUSE_SCROLL_UP) scroll_direction = -1;
	else if (evt->buttons & YUTANI_MOUSE_SCROLL_DOWN) scroll_direction = 1;

	if (scroll_direction == 1) {
		volume_lower();
		return 1;
	} else if (scroll_direction == -1) {
		volume_raise();
		return 1;
	}

	return 0;
}

struct PanelWidget * widget_init_volume(void) {
	sprite_volume_mute = malloc(sizeof(sprite_t));
	sprite_volume_low  = malloc(sizeof(sprite_t));
	sprite_volume_med  = malloc(sizeof(sprite_t));
	sprite_volume_high = malloc(sizeof(sprite_t));
	load_sprite(sprite_volume_mute, "/usr/share/icons/24/volume-mute.png");
	load_sprite(sprite_volume_low,  "/usr/share/icons/24/volume-low.png");
	load_sprite(sprite_volume_med,  "/usr/share/icons/24/volume-medium.png");
	load_sprite(sprite_volume_high, "/usr/share/icons/24/volume-full.png");

	struct PanelWidget * widget = widget_new();
	widget->width = sprite_volume_high->width + widget->pctx->extra_widget_spacing;
	widget->draw = widget_draw_volume;
	widget->click = widget_click_volume;
	widget->move  = widget_move_volume;
	widget->update = widget_update_volume;
	list_insert(widgets_enabled, widget);
	return widget;
}

