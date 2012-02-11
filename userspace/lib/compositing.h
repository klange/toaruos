/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Compositing and Window Management Library
 */

#ifndef COMPOSITING_H
#define COMPOSITING_H

#include <stdint.h>

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
	uint16_t server_width;			/* Screen resolution, width */
	uint16_t server_height;			/* Screen resolution, height */
	uint8_t server_depth;			/* Native screen depth (in bits) */

	uint32_t magic;
} wins_server_global_t;

typedef struct {
	uint8_t command_type;	/* Command or event specifier */
	size_t  packet_size;	/* Size of the *remaining* packet data */
} wins_packet_t;

#define WINS_PACKET(p) ((char *)((uintptr_t)p + sizeof(wins_packet_t)))

#define WINS_SERVER_IDENTIFIER "sys.compositor"
#define WINS_MAGIC 0xDECADE99

volatile wins_server_global_t * wins_globals;

/* Commands */
#define WC_NEWWINDOW	0x00 /* New Window */
#define WC_RESIZE		0x01 /* Resize and move an existing window */
#define WC_DESTROY		0x02 /* Destroy an existing window */
#define WC_DAMAGE		0x03 /* Damage window (redraw region) */

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

typedef struct {
	uint16_t wid;		/* or none for new window */
	uint16_t left;		/* X coordinate */
	uint16_t top;		/* Y coordinate */
	uint16_t width;		/* Width of window or region */
	uint16_t height;	/* Height of window or region */
} w_window_t;

typedef struct {
	uint16_t wid;
	uint16_t key;
} w_keyboard_t;

typedef struct {
	uint16_t wid;
	uint16_t old_x;
	uint16_t old_y;
	uint16_t new_x;
	uint16_t new_y;
	uint8_t  buttons;
} w_mouse_t;

#define MOUSE_BUTTON_LEFT		0x01
#define MOUSE_BUTTON_RIGHT		0x02
#define MOUSE_BUTTON_MIDDLE		0x04


#define SHMKEY(buf,sz,window) snprintf(buf, sz, "%s.%d.%d.%d", WINS_SERVER_IDENTIFIER, window->owner->pid, window->wid, window->bufid);


#endif
