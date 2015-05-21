/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2014 Kevin Lange
 */
#include <system.h>
#include <fs.h>
#include <pipe.h>
#include <logging.h>
#include <printf.h>

#include <ioctl.h>
#include <termios.h>
#include <ringbuffer.h>

#define TTY_BUFFER_SIZE 512

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

} pty_t;

list_t * pty_list = NULL;

#define IN(character)   ring_buffer_write(pty->in, 1, (uint8_t *)&(character))
#define OUT(character)  ring_buffer_write(pty->out, 1, (uint8_t *)&(character))

static void dump_input_buffer(pty_t * pty) {
	char * c = pty->canon_buffer;
	while (pty->canon_buflen > 0) {
		IN(*c);
		pty->canon_buflen--;
		c++;
	}
}

static void clear_input_buffer(pty_t * pty) {
	pty->canon_buflen = 0;
	pty->canon_buffer[0] = '\0';
}

static void output_process(pty_t * pty, uint8_t c) {
	if (c == '\n' && (pty->tios.c_oflag & ONLCR)) {
		uint8_t d = '\r';
		OUT(d);
	}
	OUT(c);
}

static void input_process(pty_t * pty, uint8_t c) {
	if (pty->tios.c_lflag & ICANON) {
		if (c == pty->tios.c_cc[VKILL]) {
			while (pty->canon_buflen > 0) {
				pty->canon_buflen--;
				pty->canon_buffer[pty->canon_buflen] = '\0';
				if (pty->tios.c_lflag & ECHO) {
					output_process(pty, '\010');
					output_process(pty, ' ');
					output_process(pty, '\010');
				}
			}
			return;
		}
		if (c == pty->tios.c_cc[VERASE]) {
			/* Backspace */
			if (pty->canon_buflen > 0) {
				pty->canon_buflen--;
				pty->canon_buffer[pty->canon_buflen] = '\0';
				if (pty->tios.c_lflag & ECHO) {
					output_process(pty, '\010');
					output_process(pty, ' ');
					output_process(pty, '\010');
				}
			}
			return;
		}
		if (c == pty->tios.c_cc[VINTR]) {
			if (pty->tios.c_lflag & ECHO) {
				output_process(pty, '^');
				output_process(pty, '@' + c);
				output_process(pty, '\n');
			}
			clear_input_buffer(pty);
			if (pty->fg_proc) {
				send_signal(pty->fg_proc, SIGINT);
			}
			return;
		}
		if (c == pty->tios.c_cc[VQUIT]) {
			if (pty->tios.c_lflag & ECHO) {
				output_process(pty, '^');
				output_process(pty, '@' + c);
				output_process(pty, '\n');
			}
			clear_input_buffer(pty);
			if (pty->fg_proc) {
				send_signal(pty->fg_proc, SIGQUIT);
			}
			return;
		}
		if (c == pty->tios.c_cc[VEOF]) {
			if (pty->canon_buflen) {
				dump_input_buffer(pty);
			} else {
				ring_buffer_interrupt(pty->in);
			}
			return;
		}
		pty->canon_buffer[pty->canon_buflen] = c;
		if (pty->tios.c_lflag & ECHO) {
			output_process(pty, c);
		}
		if (pty->canon_buffer[pty->canon_buflen] == '\n') {
			pty->canon_buflen++;
			dump_input_buffer(pty);
			return;
		}
		if (pty->canon_buflen == pty->canon_bufsize) {
			dump_input_buffer(pty);
			return;
		}
		pty->canon_buflen++;
		return;
	} else if (pty->tios.c_lflag & ECHO) {
		output_process(pty, c);
	}
	IN(c);
}

int pty_ioctl(pty_t * pty, int request, void * argp) {
	switch (request) {
		case IOCTLDTYPE:
			/*
			 * This is a special toaru-specific call to get a simple
			 * integer that describes the kind of device this is.
			 * It's more specific than just "character device" or "file",
			 * but for here we just need to say we're a TTY.
			 */
			return IOCTL_DTYPE_TTY;
		case TIOCSWINSZ:
			if (!argp) return -1;
			validate(argp);
			memcpy(&pty->size, argp, sizeof(struct winsize));
			/* TODO send sigwinch to fg_prog */
			return 0;
		case TIOCGWINSZ:
			if (!argp) return -1;
			validate(argp);
			memcpy(argp, &pty->size, sizeof(struct winsize));
			return 0;
		case TCGETS:
			if (!argp) return -1;
			validate(argp);
			memcpy(argp, &pty->tios, sizeof(struct termios));
			return 0;
		case TIOCSPGRP:
			if (!argp) return -1;
			validate(argp);
			pty->fg_proc = *(pid_t *)argp;
			debug_print(NOTICE, "Setting PTY group to %d", pty->fg_proc);
			return 0;
		case TIOCGPGRP:
			if (!argp) return -1;
			validate(argp);
			*(pid_t *)argp = pty->fg_proc;
			return 0;
		case TCSETS:
		case TCSETSW:
		case TCSETSF:
			if (!argp) return -1;
			validate(argp);
			if (!(((struct termios *)argp)->c_lflag & ICANON) && (pty->tios.c_lflag & ICANON)) {
				/* Switch out of canonical mode, the dump the input buffer */
				dump_input_buffer(pty);
			}
			memcpy(&pty->tios, argp, sizeof(struct termios));
			return 0;
		default:
			return -EINVAL;
	}
}

uint32_t  read_pty_master(fs_node_t * node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	pty_t * pty = (pty_t *)node->device;

	/* Standard pipe read */
	return ring_buffer_read(pty->out, size, buffer);
}
uint32_t write_pty_master(fs_node_t * node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	pty_t * pty = (pty_t *)node->device;

	size_t l = 0;
	for (uint8_t * c = buffer; l < size; ++c, ++l) {
		input_process(pty, *c);
	}

	return l;
}
void      open_pty_master(fs_node_t * node, unsigned int flags) {
	return;
}
void     close_pty_master(fs_node_t * node) {
	return;
}

uint32_t  read_pty_slave(fs_node_t * node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	pty_t * pty = (pty_t *)node->device;

	if (pty->tios.c_lflag & ICANON) {
		return ring_buffer_read(pty->in, size, buffer);
	} else {
		if (pty->tios.c_cc[VMIN] == 0) {
			return ring_buffer_read(pty->in, MIN(size, ring_buffer_unread(pty->in)), buffer);
		} else {
			return ring_buffer_read(pty->in, MIN(pty->tios.c_cc[VMIN], size), buffer);
		}
	}
}

uint32_t write_pty_slave(fs_node_t * node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	pty_t * pty = (pty_t *)node->device;

	size_t l = 0;
	for (uint8_t * c = buffer; l < size; ++c, ++l) {
		output_process(pty, *c);
	}

	return l;
}
void      open_pty_slave(fs_node_t * node, unsigned int flags) {
	return;
}
void     close_pty_slave(fs_node_t * node) {
	return;
}

/*
 * These are separate functions just in case I ever feel the need to do
 * things differently in the slave or master.
 */
int ioctl_pty_master(fs_node_t * node, int request, void * argp) {
	pty_t * pty = (pty_t *)node->device;
	return pty_ioctl(pty, request, argp);
}

int ioctl_pty_slave(fs_node_t * node, int request, void * argp) {
	pty_t * pty = (pty_t *)node->device;
	return pty_ioctl(pty, request, argp);
}

int pty_available_input(fs_node_t * node) {
	pty_t * pty = (pty_t *)node->device;
	return ring_buffer_unread(pty->in);
}

int pty_available_output(fs_node_t * node) {
	pty_t * pty = (pty_t *)node->device;
	return ring_buffer_unread(pty->out);
}

fs_node_t * pty_master_create(pty_t * pty) {
	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	memset(fnode, 0x00, sizeof(fs_node_t));

	fnode->name[0] = '\0';
	sprintf(fnode->name, "pty master");
	fnode->uid   = 0;
	fnode->gid   = 0;
	fnode->flags = FS_PIPE;
	fnode->read  =  read_pty_master;
	fnode->write = write_pty_master;
	fnode->open  =  open_pty_master;
	fnode->close = close_pty_master;
	fnode->readdir = NULL;
	fnode->finddir = NULL;
	fnode->ioctl = ioctl_pty_master;
	fnode->get_size = pty_available_output;

	fnode->device = pty;

	return fnode;
}

fs_node_t * pty_slave_create(pty_t * pty) {
	fs_node_t * fnode = malloc(sizeof(fs_node_t));
	memset(fnode, 0x00, sizeof(fs_node_t));

	fnode->name[0] = '\0';
	sprintf(fnode->name, "pty slave");
	fnode->uid   = 0;
	fnode->gid   = 0;
	fnode->flags = FS_PIPE;
	fnode->read  =  read_pty_slave;
	fnode->write = write_pty_slave;
	fnode->open  =  open_pty_slave;
	fnode->close = close_pty_slave;
	fnode->readdir = NULL;
	fnode->finddir = NULL;
	fnode->ioctl = ioctl_pty_slave;
	fnode->get_size = pty_available_input;

	fnode->device = pty;

	return fnode;
}

void pty_install(void) {
	pty_list = list_create();
}

pty_t * pty_new(struct winsize * size) {
	pty_t * pty = malloc(sizeof(pty_t));

	/* stdin linkage; characters from terminal → PTY slave */
	pty->in  = ring_buffer_create(TTY_BUFFER_SIZE);
	pty->out = ring_buffer_create(TTY_BUFFER_SIZE);

	/* Master endpoint - writes go to stdin, reads come from stdout */
	pty->master = pty_master_create(pty);

	/* Slave endpoint, reads come from stdin, writes go to stdout */
	pty->slave  = pty_slave_create(pty);

	/* TODO PTY name */
	pty->name   = 0;

	if (size) {
		memcpy(&pty->size, size, sizeof(struct winsize));
	} else {
		/* Sane defaults */
		pty->size.ws_row = 25;
		pty->size.ws_col = 80;
	}

	/* Controlling and foreground processes are set to 0 by default */
	pty->ct_proc = 0;
	pty->fg_proc = 0;

	pty->tios.c_iflag = ICRNL | BRKINT;
	pty->tios.c_oflag = ONLCR | OPOST;
	pty->tios.c_lflag = ECHO | ECHOE | ECHOK | ICANON | ISIG | IEXTEN;
	pty->tios.c_cflag = CREAD;
	pty->tios.c_cc[VEOF]   =  4; /* ^D */
	pty->tios.c_cc[VEOL]   =  0; /* Not set */
	pty->tios.c_cc[VERASE] = '\b';
	pty->tios.c_cc[VINTR]  =  3; /* ^C */
	pty->tios.c_cc[VKILL]  = 21; /* ^U */
	pty->tios.c_cc[VMIN]   =  1;
	pty->tios.c_cc[VQUIT]  = 28; /* ^\ */
	pty->tios.c_cc[VSTART] = 17; /* ^Q */
	pty->tios.c_cc[VSTOP]  = 19; /* ^S */
	pty->tios.c_cc[VSUSP] = 26; /* ^Z */
	pty->tios.c_cc[VTIME]  =  0;

	pty->canon_buffer  = malloc(TTY_BUFFER_SIZE);
	pty->canon_bufsize = TTY_BUFFER_SIZE;
	pty->canon_buflen  = 0;

	return pty;
}

int pty_create(void *size, fs_node_t ** fs_master, fs_node_t ** fs_slave) {
	pty_t * pty = pty_new(size);

	*fs_master = pty->master;
	*fs_slave  = pty->slave;

	return 0;
}
