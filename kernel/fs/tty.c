#include <system.h>
#include <fs.h>
#include <pipe.h>
#include <logging.h>

#include <ioctl.h>
#include <termios.h>
#include <ringbuffer.h>

#define TTY_BUFFER_SIZE 512

#define M_ICANON 0x01
#define M_RAW    0x02
#define M_RRAW   0x04

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

	/* line discipline modes */
	unsigned char mode;

	pid_t ct_proc; /* Controlling process (shell) */
	pid_t fg_proc; /* Foreground process (might also be shell) */

} pty_t;

list_t * pty_list = NULL;

int pty_ioctl(pty_t * pty, int request, void * argp) {
	debug_print(WARNING, "Incoming IOCTL request %d", request);
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
		default:
			return -1; /* TODO EINV... something or other */
	}
	return -1;
}

uint32_t  read_pty_master(fs_node_t * node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	pty_t * pty = (pty_t *)node->device;

	/* Standard pipe read */
	return ring_buffer_read(pty->out, size, buffer);
}
uint32_t write_pty_master(fs_node_t * node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	pty_t * pty = (pty_t *)node->device;

	/* XXX process special sequences and implement line discipline */
	return ring_buffer_write(pty->in, size, buffer);
}
void      open_pty_master(fs_node_t * node, unsigned int flags) {
	return;
}
void     close_pty_master(fs_node_t * node) {
	return;
}

uint32_t  read_pty_slave(fs_node_t * node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	pty_t * pty = (pty_t *)node->device;

	return ring_buffer_read(pty->in, size, buffer);
}
uint32_t write_pty_slave(fs_node_t * node, uint32_t offset, uint32_t size, uint8_t *buffer) {
	pty_t * pty = (pty_t *)node->device;

	return ring_buffer_write(pty->out, size, buffer);
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


