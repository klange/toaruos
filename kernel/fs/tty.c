#include <system.h>
#include <fs.h>
#include <pipe.h>
#include <logging.h>

#define TTY_BUFFER_SIZE 512

struct winsize {
	unsigned short row, col, pix_w, pix_h;
};

typedef struct {
	uint8_t * buffer;
	size_t write_ptr;
	size_t read_ptr;
	size_t size;
	uint8_t volatile lock;
	list_t * wait_queue;
} ring_buffer_t;

typedef struct pty {
	int            name;
	fs_node_t *    master;
	fs_node_t *    slave;
	struct winsize size;

	ring_buffer_t  in;
	ring_buffer_t  out;
} pty_t;

list_t * pty_list = NULL;

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
			pty->in.buffer[pty->in.write_ptr] = buffer[written];
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

	fnode->inode = (uintptr_t)pty;

	return fnode;
}

void pty_install(void) {
	pty_list = list_create();
}

pty_t * pty_new(struct winsize * size) {
	pty_t * pty = malloc(sizeof(pty_t));

	pty->in.buffer      = malloc(TTY_BUFFER_SIZE);
	pty->in.write_ptr   = 0;
	pty->in.read_ptr    = 0;
	pty->in.lock        = 0;
	pty->in.size        = TTY_BUFFER_SIZE;
	pty->in.wait_queue  = list_create();

	pty->out.buffer     = malloc(TTY_BUFFER_SIZE);
	pty->out.write_ptr  = 0;
	pty->out.read_ptr   = 0;
	pty->out.lock       = 0;
	pty->out.wait_queue = list_create();
	pty->out.size       = TTY_BUFFER_SIZE;

	pty->master = pty_master_create(pty);
	pty->slave  = pty_slave_create(pty);

	list_insert(pty_list, pty);
	pty->name = list_index_of(pty_list, pty);

	if (size) {
		memcpy(&pty->size, size, sizeof(struct winsize));
	} else {
		/* Sane defaults */
		pty->size.row = 25;
		pty->size.col = 80;
	}
	return pty;
}

int openpty(int * master, int * slave, char * name, void * _ign0, void * size) {
	if (!master || !slave) return -1;

	pty_t * pty = pty_new(size);

	*master = process_append_fd((process_t *)current_process, pty->master);
	*slave  = process_append_fd((process_t *)current_process, pty->slave);

	return 0;
}


