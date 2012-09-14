/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Compositing and Window Management Library
 */

#ifndef COMPOSITING_H
#define COMPOSITING_H

#include <stdint.h>

#include "list.h"
#include "graphics.h"

/* Connection */
typedef struct {
	/* Control flow structures */
	volatile uint8_t lock;			/* Spinlock byte */

/* LOCK REQUIRED REGION */
	volatile uint8_t client_done;	/* Client has finished work */
	volatile uint8_t server_done;	/* Server has finished work */

	/* The actual data passed back and forth */
	pid_t client_pid;				/* Actively communicating client process */
	uintptr_t event_pipe;			/* Client event pipe (ie, mouse, keyboard) */
	uintptr_t command_pipe;			/* Client command pipe (ie, resize) */
/* END LOCK REQUIRED REGION */

	/* Data about the system */
	pid_t server_pid;				/* The wins -- for signals */
	uint16_t server_width;			/* Screen resolution, width */
	uint16_t server_height;			/* Screen resolution, height */
	uint8_t server_depth;			/* Native screen depth (in bits) */

	uint32_t magic;
} wins_server_global_t;


/* Commands and Events */
typedef struct {
	uint32_t magic;
	uint8_t command_type;	/* Command or event specifier */
	size_t  packet_size;	/* Size of the *remaining* packet data */
} wins_packet_t;

#define WINS_PACKET(p) ((char *)((uintptr_t)p + sizeof(wins_packet_t)))

#define WINS_SERVER_IDENTIFIER "sys.compositor"
#define WINS_MAGIC 0xDECADE99


/* Commands */
#define WC_NEWWINDOW	0x00 /* New Window */
#define WC_RESIZE		0x01 /* Resize and move an existing window */
#define WC_DESTROY		0x02 /* Destroy an existing window */
#define WC_DAMAGE		0x03 /* Damage window (redraw region) */
#define WC_REDRAW		0x04 /* Damage window (redraw region) */
#define WC_REORDER		0x05 /* Set the Z-index for a window (request) */
#define WC_SET_ALPHA	0x06 /* Enable RGBA for compositing */

/* Events */
#define WE_KEYDOWN		0x10 /* A key has been pressed down */
#define WE_KEYUP		0x11 /* RESERVED: Key up [UNUSED] */
#define WE_MOUSEMOVE	0x20 /* The mouse has moved (to the given coordinates) */
#define WE_MOUSEENTER	0x21 /* The mouse has entered your window (at the given coordinates) */
#define WE_MOUSELEAVE	0x22 /* The mouse has left your window (at the given coordinates) */
#define WE_MOUSECLICK	0x23 /* A mouse button has been pressed that was not previously pressed */
#define WE_MOUSEUP		0x24 /* A mouse button has been released */
#define WE_NEWWINDOW	0x30 /* A new window has been created */
#define WE_RESIZED		0x31 /* Your window has been resized or moved */
#define WE_DESTROYED	0x32 /* Window has been removed */
#define WE_REDRAWN		0x34

#define WE_GROUP_MASK	0xF0
#define WE_KEY_EVT		0x10 /* Some sort of keyboard event */
#define WE_MOUSE_EVT	0x20 /* Some sort of mouse event */
#define WE_WINDOW_EVT	0x30 /* Some sort of window event */

typedef uint16_t wid_t;

typedef struct {
	wid_t    wid;		/* or none for new window */
	int16_t left;		/* X coordinate */
	int16_t top;		/* Y coordinate */
	uint16_t width;		/* Width of window or region */
	uint16_t height;	/* Height of window or region */
	uint8_t  command;	/* The command (duplicated) */
} w_window_t;

typedef struct {
	wid_t    wid;
	uint16_t key;
	uint8_t  command;
} w_keyboard_t;

typedef struct {
	wid_t    wid;
	int32_t  old_x;
	int32_t  old_y;
	int32_t  new_x;
	int32_t  new_y;
	uint8_t  buttons;
	uint8_t  command;
} w_mouse_t;

#define MOUSE_BUTTON_LEFT		0x01
#define MOUSE_BUTTON_RIGHT		0x02
#define MOUSE_BUTTON_MIDDLE		0x04


#define SHMKEY(buf,sz,win) snprintf(buf, sz, "%s.%d.%d.%d", WINS_SERVER_IDENTIFIER, win->owner->pid, win->wid, win->bufid);
#define SHMKEY_(buf,sz,win) snprintf(buf, sz, "%s.%d.%d.%d", WINS_SERVER_IDENTIFIER, getpid(), win->wid, win->bufid);


/* Windows */
typedef struct process_windows process_windows_t;

typedef struct {
	wid_t wid; /* Window identifier */
	process_windows_t * owner; /* Owning process (back ptr) */

	uint16_t width;  /* Buffer width in pixels */
	uint16_t height; /* Buffer height in pixels */

/* UNUSED IN CLIENT */
	int32_t  x; /* X coordinate of upper-left corner */
	int32_t  y; /* Y coordinate of upper-left corner */
	uint16_t z; /* Stack order */
	uint8_t  use_alpha;
/* END UNUSED IN CLIENT */

	uint8_t * buffer; /* Window buffer */
	uint16_t bufid; /* We occasionally replace the buffer; each is uniquely-indexed */
} window_t;

struct process_windows {
	uint32_t pid;

	int event_pipe;  /* Pipe to send events through */
	FILE * event_pipe_file;
	int command_pipe; /* Pipe on which we receive commands */
	FILE * command_pipe_file;

	list_t * windows;
};

volatile wins_server_global_t * wins_globals;

/* Client Windowing */

int setup_windowing ();
void teardown_windowing ();

window_t * window_create (int16_t left, int16_t top, uint16_t width, uint16_t height);
void window_resize (window_t * window, int16_t left, int16_t top, uint16_t width, uint16_t height);
void window_redraw (window_t * window, int16_t left, int16_t top, uint16_t width, uint16_t height);
void window_redraw_full (window_t * window);
void window_redraw_wait (window_t * window);
void window_destroy (window_t * window);
void window_reorder (window_t * window, uint16_t new_zed);
void window_enable_alpha (window_t * window);

w_keyboard_t * poll_keyboard();
w_mouse_t *    poll_mouse();

#define TO_WINDOW_OFFSET(x,y) (((x) - window->x) + ((y) - window->y) * window->width)
#define DIRECT_OFFSET(x,y) ((x) + (y) * window->width)

gfx_context_t * init_graphics_window(window_t * window);
gfx_context_t * init_graphics_window_double_buffer(window_t * window);

void win_use_threaded_handler();
void (*mouse_action_callback)(w_mouse_t *);

#endif
