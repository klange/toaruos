#pragma once

#include <kernel/fs.h>
#include <kernel/ringbuffer.h>
#include <sys/ioctl.h>
#include <sys/termios.h>

typedef struct pty {
	/* the PTY number */
	int            name;

	/* Master and slave endpoints */
	fs_node_t *    master;
	fs_node_t *    slave;

	/* term io "window size" struct (width/height) */
	struct winsize size;

	/* termios data structure */
	struct termios tios;

	/* directional pipes */
	ring_buffer_t * in;
	ring_buffer_t * out;

	char * canon_buffer;
	size_t canon_bufsize;
	size_t canon_buflen;

	pid_t ct_proc; /* Controlling process (shell) */
	pid_t fg_proc; /* Foreground process (might also be shell) */

	void (*write_in)(struct pty *, uint8_t);
	void (*write_out)(struct pty *, uint8_t);

	int next_is_verbatim;

	void (*fill_name)(struct pty *, char *);

} pty_t;

void tty_output_process_slave(pty_t * pty, uint8_t c);
void tty_output_process(pty_t * pty, uint8_t c);
void tty_input_process(pty_t * pty, uint8_t c);
pty_t * pty_new(struct winsize * size);
