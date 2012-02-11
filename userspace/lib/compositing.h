/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Compositing and Window Management Library
 */

#include <stdint.h>

typedef struct {
	volatile uint8_t lock;			/* Spinlock byte */
	volatile uint8_t client_done;	/* Client has finished work */
	volatile uint8_t server_done;	/* Server has finished work */
	pid_t client_pid;				/* Actively communicating client process */
	uintptr_t event_pipe;			/* Client event pipe (ie, mouse, keyboard) */
	uintptr_t command_pipe;			/* Client command pipe (ie, resize) */
} wins_server_global_t;

typedef struct {
	uint8_t command_type;	/* Command or event specifier */
	size_t  packet_size;	/* Size of the *remaining* packet data */
} wins_packet_t;

#define WINS_PACKET(p) ((char *)((uintptr_t)p + sizeof(wins_packet_t)))

/* Commands */
#define WC_NEWWINDOW	0x00 /* New Window */
#define WC_RESIZE		0x01 /* Resize existing window */
#define WC_DESTROY		0x02 /* Destroy an existing window */
#define WC_DAMAGE		0x03 /* Damage window (redraw region) */

/* Events */
#define WE_KEYDOWN		0x10 /* A key has been pressed down */
#define WE_KEYUP		0x11 /* RESERVED: Key up [UNUSED] */
#define WE_MOUSEMOVE	0x20 /* The mouse has moved (to the given coordinates) */
#define WE_MOUSEENTER	0x21 /* The mouse has entered your window (at the given coordinates) */
#define WE_MOUSELEAVE	0x22 /* The mouse has left your window (at the given coordinates) */
#define WE_MOUSECLICK	0x23 /* A mouse button has been pressed that was not previously pressed */
#define WE_RESIZE		0x30 /* Your window has been resized */

