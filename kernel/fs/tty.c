#include <system.h>
#include <fs.h>
#include <pipe.h>
#include <logging.h>

#include <termios.h>

#define TTY_BUFFER_SIZE 512

#define M_ICANON 0x01
#define M_RAW    0x02
#define M_RRAW   0x04

typedef struct {
	unsigned char * buffer;
	size_t write_ptr;
	size_t read_ptr;
	size_t size;
	uint8_t volatile lock;
	list_t * wait_queue;
} ring_buffer_t;

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
	ring_buffer_t  in;
	ring_buffer_t  out;

	/* line discipline modes */
	unsigned char mode;

	pid_t ct_proc; /* Controlling process (shell) */
	pid_t fg_proc; /* Foreground process (might also be shell) */

} pty_t;

list_t * pty_list = NULL;

int pty_ioctl(pty_t * pty, int request, void * argp) {
	debug_print(WARNING, "Incoming IOCTL request %d", request);
	switch (request) {
		case TIOCSWINSZ:
			debug_print(WARNING, "Setting!");
			memcpy(&pty->size, argp, sizeof(struct winsize));
			/* TODO send sigwinch to fg_prog */
			return 0;
		case TIOCGWINSZ:
			memcpy(argp, &pty->size, sizeof(struct winsize));
			return 0;
		default:
			return -1; /* TODO EINV... something or other */
	}
	return -1;
}

static inline size_t ring_buffer_unread(ring_buffer_t * ring_buffer) {
	if (ring_buffer->read_ptr == ring_buffer->write_ptr) {
		return 0;
	}
	if (ring_buffer->read_ptr > ring_buffer->write_ptr) {
		return (ring_buffer->size - ring_buffer->read_ptr) + ring_buffer->write_ptr;
	} else {
		return (ring_buffer->write_ptr - ring_buffer->read_ptr);
	}
}

size_t ring_buffer_size(fs_node_t * node) {
	ring_buffer_t * ring_buffer = (ring_buffer_t *)node->inode;
	return ring_buffer_unread(ring_buffer);
}

static inline size_t ring_buffer_available(ring_buffer_t * ring_buffer) {
	if (ring_buffer->read_ptr == ring_buffer->write_ptr) {
		return ring_buffer->size - 1;
	}

	if (ring_buffer->read_ptr > ring_buffer->write_ptr) {
		return ring_buffer->read_ptr - ring_buffer->write_ptr - 1;
	} else {
		return (ring_buffer->size - ring_buffer->write_ptr) + ring_buffer->read_ptr - 1;
	}
}

static inline void ring_buffer_increment_read(ring_buffer_t * ring_buffer) {
	ring_buffer->read_ptr++;
	if (ring_buffer->read_ptr == ring_buffer->size) {
		ring_buffer->read_ptr = 0;
	}
}

static inline void ring_buffer_increment_write(ring_buffer_t * ring_buffer) {
	ring_buffer->write_ptr++;
	if (ring_buffer->write_ptr == ring_buffer->size) {
		ring_buffer->write_ptr = 0;
	}
}


uint32_t  read_pty_master(fs_node_t * node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	pty_t * pty = (pty_t *)node->inode;

	/* Standard pipe read */
	size_t collected = 0;
	while (collected == 0) {
		spin_lock(&pty->out.lock);
		while (ring_buffer_unread(&pty->out) > 0 && collected < size) {
			buffer[collected] = pty->out.buffer[pty->out.read_ptr];
			ring_buffer_increment_read(&pty->out);
			collected++;
		}
		spin_unlock(&pty->out.lock);
		wakeup_queue(pty->out.wait_queue);
		if (collected == 0) {
			sleep_on(pty->out.wait_queue);
		}
	}

	return collected;
}
uint32_t write_pty_master(fs_node_t * node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	pty_t * pty = (pty_t *)node->inode;

	size_t written = 0;
	while (written < size) {
		spin_lock(&pty->in.lock);

		while (ring_buffer_available(&pty->in) > 0 && written < size) {
			unsigned char c =  buffer[written];

			/* Implement line discipline stuff here */

			pty->in.buffer[pty->in.write_ptr] = c;
			ring_buffer_increment_write(&pty->in);
			written++;
		}

		spin_unlock(&pty->in.lock);
		wakeup_queue(pty->in.wait_queue);
		if (written < size) {
			sleep_on(pty->in.wait_queue);
		}
	}

	return written;
}
void      open_pty_master(fs_node_t * node, uint8_t read, uint8_t write) {
	return;
}
void     close_pty_master(fs_node_t * node) {
	return;
}

uint32_t  read_pty_slave(fs_node_t * node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	pty_t * pty = (pty_t *)node->inode;

	debug_print(INFO, "tty read at offset=%d, size=%d", offset, size);

	/* Standard pipe read */
	size_t collected = 0;
	while (collected == 0) {
		spin_lock(&pty->in.lock);
		while (ring_buffer_unread(&pty->in) > 0 && collected < size) {
			buffer[collected] = pty->in.buffer[pty->in.read_ptr];
			ring_buffer_increment_read(&pty->in);
			collected++;
		}
		spin_unlock(&pty->in.lock);
		wakeup_queue(pty->in.wait_queue);
		if (collected == 0) {
			sleep_on(pty->in.wait_queue);
		}
	}

	debug_print(INFO, "} completed");

	return collected;
}
uint32_t write_pty_slave(fs_node_t * node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	pty_t * pty = (pty_t *)node->inode;

	size_t written = 0;
	while (written < size) {
		spin_lock(&pty->out.lock);

		while (ring_buffer_available(&pty->out) > 0 && written < size) {
			pty->out.buffer[pty->out.write_ptr] = buffer[written];
			ring_buffer_increment_write(&pty->out);
			written++;
		}

		spin_unlock(&pty->out.lock);
		wakeup_queue(pty->out.wait_queue);
		if (written < size) {
			sleep_on(pty->out.wait_queue);
		}
	}

	return written;
}
void      open_pty_slave(fs_node_t * node, uint8_t read, uint8_t write) {
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
	pty_t * pty = (pty_t *)node->inode;
	return pty_ioctl(pty, request, argp);
}

int ioctl_pty_slave(fs_node_t * node, int request, void * argp) {
	pty_t * pty = (pty_t *)node->inode;
	return pty_ioctl(pty, request, argp);
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

	fnode->inode = (uintptr_t)pty;

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

	fnode->inode = (uintptr_t)pty;

	return fnode;
}

void pty_install(void) {
	pty_list = list_create();
}

pty_t * pty_new(struct winsize * size) {
	pty_t * pty = malloc(sizeof(pty_t));

	/* stdin linkage; characters from terminal → PTY slave */
	pty->in.buffer      = malloc(TTY_BUFFER_SIZE);
	pty->in.write_ptr   = 0;
	pty->in.read_ptr    = 0;
	pty->in.lock        = 0;
	pty->in.size        = TTY_BUFFER_SIZE;
	pty->in.wait_queue  = list_create();

	/* stdout linkage; characters from client application → terminal */
	pty->out.buffer     = malloc(TTY_BUFFER_SIZE);
	pty->out.write_ptr  = 0;
	pty->out.read_ptr   = 0;
	pty->out.lock       = 0;
	pty->out.wait_queue = list_create();
	pty->out.size       = TTY_BUFFER_SIZE;

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

	/* tty mode (cooked, raw, etc.) */
	pty->mode = M_ICANON;

	/* Controlling and foreground processes are set to 0 by default */
	pty->ct_proc = 0;
	pty->fg_proc = 0;

	return pty;
}

int openpty(int * master, int * slave, char * name, void * _ign0, void * size) {
	/* We require a place to put these when we are done. */
	if (!master || !slave) return -1;
	if (validate_safe(master) || validate_safe(slave)) return -1;
	if (validate_safe(size)) return -1;

	/* Create a new pseudo terminal */
	pty_t * pty = pty_new(size);

	/* Append the master and slave to the calling process */
	*master = process_append_fd((process_t *)current_process, pty->master);
	*slave  = process_append_fd((process_t *)current_process, pty->slave);

	/* Return success */
	return 0;
}


