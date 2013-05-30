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
#include <assert.h>
#include <sys/stat.h>
#include <signal.h>

#include "window.h"
#include "pthread.h"

extern FILE *fdopen(int fildes, const char *mode);

static int event_pipe;
static int mouse_event_pipe;
static int gobble_mouse_events = 1;

#define LOCK(lock) while (__sync_lock_test_and_set(&lock, 0x01)) { syscall_yield(); };
#define UNLOCK(lock) __sync_lock_release(&lock);

#define WIN_B 4

volatile wins_server_global_t * wins_globals = NULL;
process_windows_t * process_windows = NULL;

#define get_window wins_get_window

window_t * wins_get_window (wid_t wid) {
	foreach(n, process_windows->windows) {
		window_t * w = (window_t *)n->value;
		if (w->wid == wid) {
			return w;
		}
	}

	return NULL;
}

void (*mouse_action_callback)(w_mouse_t *) = NULL;
void (*resize_window_callback)(window_t *) = NULL;
void (*focus_changed_callback)(window_t *) = NULL;

/* Window Object Management */
/*XXX ... */
window_t * init_window_client (process_windows_t * pw, wid_t wid, int32_t x, int32_t y, uint16_t width, uint16_t height, uint16_t index) {

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
	window->focused = 0;

	char key[1024];
	SHMKEY_(key, 1024, window);

	size_t size = (width * height * WIN_B);
	window->buffer = (uint8_t *)syscall_shm_obtain(key, &size);

	if (!window->buffer) {
		fprintf(stderr, "[%d] [window] Could not create a buffer for a new window for pid %d!", getpid(), pw->pid);
		free(window);
		return NULL;
	}

	list_insert(pw->windows, window);

	return window;
}

void free_window_client (window_t * window) {
	/* Free the window buffer */
	if (!window) return;
	char key[256];
	SHMKEY(key, 256, window);
	syscall_shm_release(key);

	/* Now, kill the object itself */
	process_windows_t * pw = window->owner;
	node_t * n = list_find(pw->windows, window);
	if (n) {
		list_delete(pw->windows, n);
		free(n);
	}
}

void resize_window_buffer_client (window_t * window, int16_t left, int16_t top, uint16_t width, uint16_t height) {

	if (!window) {
		return;
	}
	/* If the window has enlarged, we need to create a new buffer */
	if ((width * height) > (window->width * window->height)) {
		/* Release the old buffer */
		char key[256], keyn[256];
		SHMKEY_(key, 256, window);

		/* Create the new one */
		window->bufid++;
		SHMKEY_(keyn, 256, window);

		size_t size = (width * height * WIN_B);

		char * new_buffer = (uint8_t *)syscall_shm_obtain(keyn, &size);

		window->buffer = new_buffer;

		syscall_shm_release(key);
	}

	window->width = width;
	window->height = height;
}


/* Command Dispatch */

uint8_t volatile wins_command_lock;
uint8_t volatile wins_command_recvd;
window_t volatile * wins_last_new;


void wins_send_command (wid_t wid, int16_t left, int16_t top, uint16_t width, uint16_t height, int command, int wait_for_reply) {

	/* Construct the header and packet */
	wins_packet_t header;
	header.magic = WINS_MAGIC;
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

	fwrite(&header, sizeof(wins_packet_t), 1, process_windows->command_pipe_file);
	fwrite(&packet, sizeof(w_window_t),    1, process_windows->command_pipe_file);
	fflush(process_windows->command_pipe_file);

	/* Now wait for the command to be processed before returning */
	if (wait_for_reply) {
		syscall_send_signal(process_windows->pid, SIGWINEVENT);
		while((wins_command_recvd & 0xF) != (command & 0xF)) {
			syscall_yield();
		}
	}

	UNLOCK(wins_command_lock);
}


window_t * window_create (int16_t left, int16_t top, uint16_t width, uint16_t height) {
	wins_send_command(0, left, top, width, height, WC_NEWWINDOW, 1);

	while (!wins_last_new) {
		syscall_yield();
	}

	return (window_t *)wins_last_new;
}

void window_resize (window_t * window, int16_t left, int16_t top, uint16_t width, uint16_t height) {
	wins_send_command(window->wid, left, top, width, height, WC_RESIZE, 1);
}

void window_redraw (window_t * window, int16_t left, int16_t top, uint16_t width, uint16_t height) {
	wins_send_command(window->wid, left, top, width, height, WC_DAMAGE, 0);
}

void window_redraw_full (window_t * window) {
	wins_send_command(window->wid, 0, 0, window->width, window->height, WC_DAMAGE, 0);
}

void window_redraw_wait (window_t * window) {
	wins_send_command(window->wid, 0, 0, window->width, window->height, WC_REDRAW, 1);
}

void window_destroy (window_t * window) {
	wins_send_command(window->wid, 0, 0, 0, 0, WC_DESTROY, 1);
	free_window_client(window);
}

void window_reorder (window_t * window, uint16_t new_zed) {
	wins_send_command(window->wid, new_zed, 0, 0, 0, WC_REORDER, 0);
}

void window_enable_alpha (window_t * window) {
	wins_send_command(window->wid, 1, 0, 0, 0, WC_SET_ALPHA, 0);
}

void window_disable_alpha (window_t * window) {
	wins_send_command(window->wid, 0, 0, 0, 0, WC_SET_ALPHA, 0);
}

/* Event Processing (invoked by signal only) */

#define MAX_UNREAD_KEY_EVENTS   200
#define MAX_UNREAD_MOUSE_EVENTS 200

static int kbd_read = 0;
static int mouse_read = 0;

w_keyboard_t * poll_keyboard () {
	w_keyboard_t * evt = NULL;

	read(event_pipe, &evt, sizeof(evt));
	kbd_read--;

	return evt;
}

w_keyboard_t * poll_keyboard_async() {
	w_keyboard_t * evt = NULL;

	struct stat _stat;
	fstat(event_pipe, &_stat);
	if (_stat.st_size >= sizeof(evt)) {
		read(event_pipe, &evt, sizeof(evt));
		kbd_read--;
	}

	return evt;
}

static void process_key_evt (uint8_t command, w_keyboard_t * evt) {
	/* Push the event onto a buffer for the process to poll */

	kbd_read++;
	if (kbd_read > MAX_UNREAD_KEY_EVENTS) {
		char garbage[sizeof(evt)];
		read(event_pipe, &garbage, sizeof(evt));
		kbd_read--;
	}

	write(event_pipe, &evt, sizeof(evt));
}

w_mouse_t * poll_mouse () {
	w_mouse_t * evt = NULL;

	read(mouse_event_pipe, &evt, sizeof(evt));
	mouse_read--;

	return evt;
}

static void process_mouse_evt (uint8_t command, w_mouse_t * evt) {
	/* Push the event onto a buffer for the process to poll */
	if (gobble_mouse_events) return; /* om nom nom */
	mouse_read++;
	if (mouse_read > MAX_UNREAD_MOUSE_EVENTS) {
		char garbage[sizeof(evt)];
		read(mouse_event_pipe, &garbage, sizeof(evt));
		mouse_read--;
	}
	if (mouse_action_callback) {
		mouse_action_callback(evt);
	}
	write(mouse_event_pipe, &evt, sizeof(evt));
}


static void process_window_evt (uint8_t command, w_window_t evt) {
	switch (command) {
		window_t * window = NULL;
		case WE_NEWWINDOW:
			window = init_window_client(process_windows, evt.wid, evt.left, evt.top, evt.width, evt.height, 0);
			wins_last_new = window;
			break;

		case WE_FOCUSCHG:
			window = get_window(evt.wid);
			if (window) {
				window->focused = evt.left;
				if (focus_changed_callback) {
					focus_changed_callback(window);
				}
			}
			break;

		case WE_RESIZED:
			/* XXX: We need a lock or something to contend the window buffer */
			window = get_window(evt.wid);
			if (!window) {
				fprintf(stderr, "[%d] [window] SEVERE: wins sent WE_RESIZED for window we don't have!\n", getpid());
				return;
			}
			resize_window_buffer_client(window, evt.left, evt.top, evt.width, evt.height);
			if (resize_window_callback) {
				resize_window_callback(window);
			}
			break;
	}

	wins_command_recvd = command;
}

static void process_evt () {
	/* Wait for and process one event */

	wins_packet_t header;
	read(process_windows->event_pipe, &header, sizeof(wins_packet_t));

	while (header.magic != WINS_MAGIC) {
		/* REALIGN!! */
		memcpy(&header, (void *)((uintptr_t)&header + 1), (sizeof(header) - 1));
		read(process_windows->event_pipe, (char *)((uintptr_t)&header + sizeof(header) - 1), 1);
	}

	/* Determine type, read, and dispatch */
	switch (header.command_type & WE_GROUP_MASK) {
		case WE_MOUSE_EVT: {
			w_mouse_t * mevt = malloc(sizeof(w_mouse_t));
			read(process_windows->event_pipe, mevt, sizeof(w_mouse_t));
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
			fprintf(stderr, "[%d] [window] WARN: Received unknown event type %d, 0x%x\n", getpid(), header.command_type, header.packet_size);
			struct stat buf;
			fstat(process_windows->event_pipe, &buf);
			char devnull[1];
			for (uint32_t i = 0; i < buf.st_size; ++i) {
				read(process_windows->event_pipe, devnull, 1);
			}
			break;
	}
}

static void sig_process_evt(int sig) {
	if (!process_windows) return;
	struct stat buf;
	fstat(process_windows->event_pipe, &buf);
	do {
		process_evt();
		fstat(process_windows->event_pipe, &buf);
	} while (buf.st_size);
	wins_packet_t header;

}

void install_signal_handlers () {
	syscall_signal(SIGWINEVENT, sig_process_evt); // SIGWINEVENT
}

static void ignore(int sig) {
	return;
}

void * win_threaded_event_processor(void * garbage) {
	while (1) {
		process_evt();
	}
}

static int _disabled_sigwinevent = 0;

void win_use_threaded_handler() {
	_disabled_sigwinevent = 1;
	syscall_signal(SIGWINEVENT, ignore); // SIGWINEVENT
	pthread_t event_thread;
	pthread_create(&event_thread, NULL, win_threaded_event_processor, NULL);
	gobble_mouse_events = 0;
}

void win_sane_events() {
	_disabled_sigwinevent = 1;
	syscall_signal(SIGWINEVENT, ignore); // SIGWINEVENT
	gobble_mouse_events = 0;
}

wins_packet_t * get_window_events() {
	/* Wait for and return one window event; process the core window events, but also return them for reference */

	wins_packet_t header;
	read(process_windows->event_pipe, &header, sizeof(wins_packet_t));

	while (header.magic != WINS_MAGIC) {
		/* REALIGN!! */
		memcpy(&header, (void *)((uintptr_t)&header + 1), (sizeof(header) - 1));
		read(process_windows->event_pipe, (char *)((uintptr_t)&header + sizeof(header) - 1), 1);
	}

	wins_packet_t * out = malloc(sizeof(wins_packet_t) + header.packet_size);
	memcpy(out, &header, sizeof(wins_packet_t));
	read(process_windows->event_pipe, (char *)((uintptr_t)out + sizeof(wins_packet_t)), header.packet_size);

	return out;
}

wins_packet_t * get_window_events_async() {
	struct stat _stat;
	fstat(process_windows->event_pipe, &_stat);
	if (_stat.st_size >= sizeof(wins_packet_t)) {
		return get_window_events();
	} else {
		return NULL;
	}
}



/* Initial Connection */

int wins_connect() {
	if (wins_globals) {
		/* Already connected. Bailing. */
		return 0;
	}

	size_t size = sizeof(wins_server_global_t);
	wins_globals = (volatile wins_server_global_t *)syscall_shm_obtain(WINS_SERVER_IDENTIFIER, &size);
	if (!wins_globals) {
		fprintf(stderr, "[%d] [window] Unable to connect with wins through shared memory.\n", getpid());
		return EACCES;
	}

	/* Verify magic */
	if (wins_globals->magic != WINS_MAGIC) {
		/* If the magic is incorrent, this probably means the server isn't available. */
		fprintf(stderr, "[%d] [window] Window server [%p] size claims to be %dx%d\n", getpid(), wins_globals, wins_globals->server_width, wins_globals->server_height);
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

	while (!wins_globals->server_done) {
		syscall_yield();
	}


	assert(process_windows && "process_windows was not initialized!");
	process_windows->pid          = wins_globals->server_pid;
	process_windows->event_pipe   = syscall_get_fd(wins_globals->event_pipe);
	process_windows->command_pipe = syscall_get_fd(wins_globals->command_pipe);
	process_windows->command_pipe_file = fdopen(process_windows->command_pipe, "w");

	if (process_windows->event_pipe < 0) {
		fprintf(stderr, "ERROR: Failed to initialize an event pipe!\n");
		return 1;
	}

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
#if 0
	syscall_shm_release(WINS_SERVER_IDENTIFIER);
#endif
	if (wins_globals) {
		//free((wins_server_global_t *)wins_globals);
		wins_globals = NULL;
	}
}


/* Client Setup/Teardown */

int setup_windowing () {
	if (!process_windows) {
		process_windows = malloc(sizeof(process_windows_t));
		process_windows->windows = list_create();
	}

	event_pipe = syscall_mkpipe();
	mouse_event_pipe = syscall_mkpipe();

	install_signal_handlers();

	return wins_connect();
}

void teardown_windowing () {
	if (process_windows) {
		if (_disabled_sigwinevent) {
			syscall_signal(SIGWINEVENT, sig_process_evt); // SIGWINEVENT
		}
		window_t * window;
		node_t   * node;
		while ((node = list_pop(process_windows->windows)) != NULL) {
			window = node->value;
			if (!window) break;
			window_destroy(window);
		}

		free(process_windows->windows);
		free(process_windows);
		process_windows = NULL;
	}

	wins_disconnect();
}
