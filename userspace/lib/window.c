/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Window Library
 */

#include <syscall.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <assert.h>
#include <sys/stat.h>

#include "window.h"

#if 1
DEFN_SYSCALL2(shm_obtain, 35, char *, int)
DEFN_SYSCALL1(shm_release, 36, char *)
DEFN_SYSCALL2(send_signal, 37, int, int)
DEFN_SYSCALL2(sys_signal, 38, int, int)
DEFN_SYSCALL2(share_fd, 39, int, int)
DEFN_SYSCALL1(get_fd, 40, int)
#endif

#define LOCK(lock) while (__sync_lock_test_and_set(&lock, 0x01));
#define UNLOCK(lock) __sync_lock_release(&lock);

#define WIN_B 4

volatile wins_server_global_t * wins_globals = NULL;
process_windows_t * process_windows = NULL;

static window_t * get_window (wid_t wid) {
	foreach(n, process_windows->windows) {
		window_t * w = (window_t *)n->value;
		if (w->wid == wid) {
			return w;
		}
	}

	return NULL;
}

/* Window Object Management */

window_t * init_window (process_windows_t * pw, wid_t wid, int32_t x, int32_t y, uint16_t width, uint16_t height, uint16_t index) {

	printf("Creating window id %d (+%d,%d:%dx%d)\n", wid, x, y, width, height);

	window_t * window = malloc(sizeof(window_t));
	if (!window) {
		fprintf(stderr, "[%d] [window] Could not malloc a window_t!", getpid());
		return NULL;
	}

	window->owner = pw;
	window->wid = wid;
	window->bufid = 0;

	window->width  = width;
	window->height = height;
	window->x = x;
	window->y = y;
	window->z = index;

	char key[1024];
	SHMKEY(key, 1024, window);

	/* And now the fucked up stuff happens */
	window->buffer = (uint8_t *)syscall_shm_obtain(key, (width * height * WIN_B));

	if (!window->buffer) {
		fprintf(stderr, "[%d] [window] Could not create a buffer for a new window for pid %d!", getpid(), pw->pid);
		free(window);
		return NULL;
	}

	list_insert(pw->windows, window);

	return window;
}

void free_window (window_t * window) {
	/* Free the window buffer */
	char key[256];
	SHMKEY(key, 256, window);
	syscall_shm_release(key);

	/* Now, kill the object itself */
	process_windows_t * pw = window->owner;

	node_t * n = list_find(pw->windows, window);
	list_delete(pw->windows, n);
	free(n);

#if 0
	/* Does the owner have any windows themselves? */
	if (pw->windows->length == 0) {
		delete_process(pw);
	}
#endif
}

void resize_window_buffer (window_t * window, uint16_t left, uint16_t top, uint16_t width, uint16_t height) {

	/* If the window has enlarged, we need to create a new buffer */
	if ((width * height) > (window->width * window->height)) {
		/* Release the old buffer */
		char key[256];
		SHMKEY(key, 256, window);
		syscall_shm_release(key);

		/* Create the new one */
		window->bufid++;
		SHMKEY(key, 256, window);
		window->buffer = (uint8_t *)syscall_shm_obtain(key, (width * height * WIN_B));
		memset(window->buffer, 0, (width * height * WIN_B));
	}

	window->x = left;
	window->y = top;
	window->width = width;
	window->height = height;
}


/* Drawing Tools */

void window_set_point(window_t * window, uint16_t x, uint16_t y, uint32_t color) {
	if (x < 0 || y < 0 || x >= window->width || y >= window->height) {
		return;
	}

	((uint32_t *)window->buffer)[DIRECT_OFFSET(x,y)] = color;
}

void window_draw_line(window_t * window, uint16_t x0, uint16_t x1, uint16_t y0, uint16_t y1, uint32_t color) {
	int deltax = abs(x1 - x0);
	int deltay = abs(y1 - y0);
	int sx = (x0 < x1) ? 1 : -1;
	int sy = (y0 < y1) ? 1 : -1;
	int error = deltax - deltay;
	while (1) {
		window_set_point(window, x0, y0, color);
		if (x0 == x1 && y0 == y1) break;
		int e2 = 2 * error;
		if (e2 > -deltay) {
			error -= deltay;
			x0 += sx;
		}
		if (e2 < deltax) {
			error += deltax;
			y0 += sy;
		}
	}
}

static int32_t min(int32_t a, int32_t b) {
	return (a < b) ? a : b;
}

void window_draw_sprite(window_t * window, sprite_t * sprite, uint16_t x, uint16_t y) {
	int x_hi = min(sprite->width, (window->width - x));
	int y_hi = min(sprite->height, (window->height - y));

	for (uint16_t _y = 0; _y < y_hi; ++_y) {
		for (uint16_t _x = 0; _x < x_hi; ++_x) {
			if (sprite->alpha) {
				/* Technically, unsupported! */
				window_set_point(window, x + _x, y + _y, SPRITE(sprite, _x, _y));
			} else {
				if (SPRITE(sprite,_x,_y) != sprite->blank) {
					window_set_point(window, x + _x, y + _y, SPRITE(sprite, _x, _y));
				}
			}
		}
	}
}

void window_fill(window_t *window, uint32_t color) {
	for (uint16_t i = 0; i < window->height; ++i) {
		for (uint16_t j = 0; j < window->width; ++j) {
			((uint32_t *)window->buffer)[DIRECT_OFFSET(j,i)] = color;
		}
	}
}


/* Command Dispatch */

uint8_t volatile wins_command_lock;
uint8_t volatile wins_command_recvd;
window_t volatile * wins_last_new;


void wins_send_command (wid_t wid, uint16_t left, uint16_t top, uint16_t width, uint16_t height, int command, int wait_for_reply) {

	/* Construct the header and packet */
	wins_packet_t header;
	header.command_type = command;
	header.packet_size = sizeof(w_window_t);

	w_window_t packet;
	packet.wid = wid;
	packet.left = left;
	packet.top = top;
	packet.width = width;
	packet.height = height;

	/* Send them */
	LOCK(wins_command_lock);
	wins_command_recvd = 0xFF; // XXX: Will this work?

	write(process_windows->command_pipe, &header, sizeof(wins_packet_t));
	write(process_windows->command_pipe, &packet, sizeof(w_window_t));
	syscall_send_signal(process_windows->pid, command);

	/* Now wait for the command to be processed before returning */
	if (wait_for_reply) {
		while((wins_command_recvd & 0xF) != (command & 0xF)) {}
	}

#if 0
	/* Were we waiting for that? */
	if (wins_command_recvd != command) {
		fprintf(stderr, "[%d] [window] WARN: Waited for %d, got %d\n", getpid(), command, wins_command_recvd);
	}
#endif

	UNLOCK(wins_command_lock);
}


window_t * window_create (uint16_t left, uint16_t top, uint16_t width, uint16_t height) {
	wins_send_command(0, left, top, width, height, WC_NEWWINDOW, 1);

	assert(wins_last_new);
	return (window_t *)wins_last_new;
}

void window_resize (window_t * window, uint16_t left, uint16_t top, uint16_t width, uint16_t height) {
	wins_send_command(window->wid, left, top, width, height, WC_RESIZE, 1);
}

void window_redraw (window_t * window, uint16_t left, uint16_t top, uint16_t width, uint16_t height) {
	wins_send_command(window->wid, left, top, width, height, WC_DAMAGE, 0);
}

void window_redraw_full (window_t * window) {
	wins_send_command(window->wid, 0, 0, window->width, window->height, WC_DAMAGE, 0);
}

void window_destroy (window_t * window) {
	wins_send_command(window->wid, 0, 0, 0, 0, WC_DESTROY, 0);
	free_window(window);
}


/* Event Processing (invoked by signal only) */

uint8_t volatile key_evt_buffer_lock;
list_t * key_evt_buffer;

w_keyboard_t * poll_keyboard () {
	w_keyboard_t * evt;

	LOCK(key_evt_buffer_lock);
	if (key_evt_buffer->length > 0) {
		node_t * n = list_pop(key_evt_buffer);
		evt = (w_keyboard_t *)n->value;
		free(n);
	}
	UNLOCK(key_evt_buffer_lock);

	return evt;
}

static void process_key_evt (uint8_t command, w_keyboard_t * evt) {
	/* Push the event onto a buffer for the process to poll */
	LOCK(key_evt_buffer_lock);
	list_insert(key_evt_buffer, evt);
	UNLOCK(key_evt_buffer_lock);
}


uint8_t volatile mouse_evt_buffer_lock;
list_t * mouse_evt_buffer;

w_mouse_t * poll_mouse () {
	w_mouse_t * evt;

	LOCK(mouse_evt_buffer_lock);
	if (mouse_evt_buffer->length > 0) {
		node_t * n = list_pop(mouse_evt_buffer);
		evt = (w_mouse_t *)n->value;
		free(n);
	}
	UNLOCK(mouse_evt_buffer_lock);

	return evt;
}

static void process_mouse_evt (uint8_t command, w_mouse_t * evt) {
	/* Push the event onto a buffer for the process to poll */
	LOCK(mouse_evt_buffer_lock);
	list_insert(mouse_evt_buffer, evt);
	UNLOCK(mouse_evt_buffer_lock);
}


static void process_window_evt (uint8_t command, w_window_t evt) {
	switch (command) {
		window_t * window = NULL;
		case WE_NEWWINDOW:
			window = init_window(process_windows, evt.wid, evt.left, evt.top, evt.width, evt.height, 0);
			wins_last_new = window;
			break;

		case WE_RESIZED:
			/* XXX: We need a lock or something to contend the window buffer */
			window = get_window(evt.wid);
			if (!window) {
				fprintf(stderr, "[%d] [window] SEVERE: wins sent WE_RESIZED for window we don't have!\n", getpid());
			}
			resize_window_buffer(window, evt.left, evt.top, evt.width, evt.height);
			break;
	}

	wins_command_recvd = command;
}

static void process_evt (int sig) {
	/* Are there any messages in this process's event pipe? */
	struct stat buf;
	fstat(process_windows->event_pipe, &buf);

	/* Read them all out */
	while (buf.st_size > 0) {
		wins_packet_t header;
		read(process_windows->event_pipe, &header, sizeof(wins_packet_t));

		/* Determine type, read, and dispatch */
		switch (header.command_type | WE_GROUP_MASK) {
			case WE_MOUSE_EVT: {
				w_mouse_t * mevt = malloc(sizeof(w_mouse_t));
				read(process_windows->event_pipe, &mevt, sizeof(w_mouse_t));
				process_mouse_evt(header.command_type, mevt);
				break;
			}

			case WE_KEY_EVT: {
				w_keyboard_t * kevt = malloc(sizeof(w_keyboard_t));
				read(process_windows->event_pipe, kevt, sizeof(w_keyboard_t));
				process_key_evt(header.command_type, kevt);
				break;
			}

			case WE_WINDOW_EVT: {
				w_window_t wevt;
				read(process_windows->event_pipe, &wevt, sizeof(w_window_t));
				process_window_evt(header.command_type, wevt);
				break;
			}

			default:
				fprintf(stderr, "[%d] [window] WARN: Received unknown event type %d\n", getpid(), header.command_type);
				void * nullbuf = malloc(header.packet_size);
				read(process_windows->command_pipe, nullbuf, header.packet_size);
				free(nullbuf);
				break;
		}

		fstat(process_windows->event_pipe, &buf);
	}
}

void install_signal_handlers () {
	syscall_sys_signal(35, (uintptr_t)process_evt); // SIGWINEVENT
	key_evt_buffer = list_create();
	mouse_evt_buffer = list_create();
}


/* Initial Connection */

int wins_connect() {
	if (wins_globals) {
		/* Already connected. Bailing. */
		return 0;
	}

	wins_globals = (volatile wins_server_global_t *)syscall_shm_obtain(WINS_SERVER_IDENTIFIER, sizeof(wins_server_global_t));
	if (!wins_globals) {
		fprintf(stderr, "[%d] [window] Unable to connect with wins through shared memory.\n", getpid());
		return EACCES;
	}

	/* Verify magic */
	if (wins_globals->magic != WINS_MAGIC) {
		/* If the magic is incorrent, this probably means the server isn't available. */
		fprintf(stderr, "[%d] [window] Window server not available (expected magic %x, got %x)\n", getpid(), WINS_MAGIC, wins_globals->magic);
		syscall_shm_release(WINS_SERVER_IDENTIFIER);
		return EAGAIN;
	}

	/* Enter handshake lock */
	LOCK(wins_globals->lock);
	/* Lock Obtained */

	/* Share client PID */
	wins_globals->client_pid = getpid();
	wins_globals->server_done = 0;

	/* Mark us as done and wait for the server */
	wins_globals->client_done = 1;
	while (!wins_globals->server_done);

	assert(process_windows && "process_windows was not initialized!");
	process_windows->pid          = wins_globals->server_pid;
	process_windows->event_pipe   = syscall_get_fd(wins_globals->event_pipe);
	process_windows->command_pipe = syscall_get_fd(wins_globals->command_pipe);

	/* Reset client status for next client */
	wins_globals->client_done  = 0;
	wins_globals->event_pipe   = 0;
	wins_globals->command_pipe = 0;
	wins_globals->client_pid   = 0;
	wins_globals->server_done  = 0;

	/* Done with lock */
	UNLOCK(wins_globals->lock);
	return 0;
}

int wins_disconnect() {
	syscall_shm_release(WINS_SERVER_IDENTIFIER);
	if (wins_globals) {
		free((wins_server_global_t *)wins_globals);
		wins_globals = NULL;
	}
}


/* Client Setup/Teardown */

int setup_windowing () {
	if (!process_windows) {
		process_windows = malloc(sizeof(process_windows_t));
		process_windows->windows = list_create();
	}

	install_signal_handlers();

	return wins_connect();
}

void teardown_windowing () {
	if (process_windows) {
		window_t * window;
		while ((window = (window_t *)list_pop(process_windows->windows)) != NULL) {
			window_destroy(window);
		}

		free(process_windows->windows);
		free(process_windows);
		process_windows = NULL;
	}

	wins_disconnect();
}
