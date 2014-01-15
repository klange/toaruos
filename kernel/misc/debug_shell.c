/*
 * Kernel Debug Shell
 */
#include <system.h>
#include <fs.h>
#include <logging.h>
#include <process.h>
#include <version.h>
#include <termios.h>
#include <tokenize.h>
#include <hashmap.h>
#include <pci.h>
#include <pipe.h>

#include <debug_shell.h>

/*
 * This is basically the same as a userspace buffered/unbuffered
 * termio call. These are the same sorts of things I would use in
 * a text editor in userspace, but with the internal kernel calls
 * rather than system calls.
 */
static struct termios old;

void set_unbuffered(fs_node_t * dev) {
	ioctl_fs(dev, TCGETS, &old);
	struct termios new = old;
	new.c_lflag &= (~ICANON & ~ECHO);
	ioctl_fs(dev, TCSETSF, &new);
}

void set_buffered(fs_node_t * dev) {
	ioctl_fs(dev, TCSETSF, &old);
}

/*
 * TODO move this to the printf module
 */
void fs_printf(fs_node_t * device, char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	char buffer[1024];
	vasprintf(buffer, fmt, args);
	va_end(args);

	write_fs(device, 0, strlen(buffer), (uint8_t *)buffer);
}

/*
 * Quick readline implementation.
 *
 * Most of these TODOs are things I've done already in older code:
 * TODO tabcompletion would be nice
 * TODO history is also nice
 */
int debug_shell_readline(fs_node_t * dev, char * linebuf, int max) {
	int read = 0;
	set_unbuffered(dev);
	while (read < max) {
		uint8_t buf[1];
		int r = read_fs(dev, 0, 1, (unsigned char *)buf);
		if (!r) {
			debug_print(WARNING, "Read nothing?");
			continue;
		}
		linebuf[read] = buf[0];
		if (buf[0] == '\n') {
			fs_printf(dev, "\n");
			linebuf[read] = 0;
			break;
		} else if (buf[0] == 0x08) {
			if (read > 0) {
				fs_printf(dev, "\010 \010");
				read--;
				linebuf[read] = 0;
			}
			continue;
		}
		fs_printf(dev, "%c", buf[0]);
		read += r;
	}
	set_buffered(dev);
	return read;
}

/*
 * Tasklet for running a userspace application.
 */
void debug_shell_run_sh(void * data, char * name) {

	char * argv[] = {
		"/bin/sh",
		NULL
	};
	int argc = 0;
	while (argv[argc]) {
		argc++;
	}
	system(argv[0], argc, argv); /* Run shell */

	task_exit(42);
}

/*
 * We're going to have a list of shell commands.
 * We'll search through it linearly because I don't
 * care to write a hashmap right now. Maybe later.
 */
struct shell_command {
	char * name;
	int (*function) (fs_node_t * tty, int argc, char * argv[]);
	char * description;
};

hashmap_t * shell_commands_map = NULL;

/*
 * Shell commands
 */
static int shell_create_userspace_shell(fs_node_t * tty, int argc, char * argv[]) {
	int pid = create_kernel_tasklet(debug_shell_run_sh, "[[k-sh]]", NULL);
	fs_printf(tty, "Shell started with pid = %d\n", pid);
	process_t * child_task = process_from_pid(pid);
	sleep_on(child_task->wait_queue);
	return child_task->status;
}

static int shell_echo(fs_node_t * tty, int argc, char * argv[]) {
	for (int i = 1; i < argc; ++i) {
		fs_printf(tty, "%s ", argv[i]);
	}
	fs_printf(tty, "\n");
	return 0;
}

static int shell_help(fs_node_t * tty, int argc, char * argv[]) {
	list_t * hash_keys = hashmap_keys(shell_commands_map);

	foreach(_key, hash_keys) {
		char * key = (char *)_key->value;
		struct shell_command * c = hashmap_get(shell_commands_map, key);

		fs_printf(tty, "%s - %s\n", c->name, c->description);
	}

	list_free(hash_keys);
	free(hash_keys);

	return 0;
}

static int shell_cd(fs_node_t * tty, int argc, char * argv[]) {
	if (argc < 2) {
		return -1;
	}
	char * newdir = argv[1];
	char * path = canonicalize_path(current_process->wd_name, newdir);
	fs_node_t * chd = kopen(path, 0);
	if (chd) {
		if ((chd->flags & FS_DIRECTORY) == 0) {
			return -1;
		}
		free(current_process->wd_name);
		current_process->wd_name = malloc(strlen(path) + 1);
		memcpy(current_process->wd_name, path, strlen(path) + 1);
		return 0;
	} else {
		return -1;
	}
}

static int shell_ls(fs_node_t * tty, int argc, char * argv[]) {
	/* Okay, we're going to take the working directory... */
	fs_node_t * wd = kopen(current_process->wd_name, 0);
	uint32_t index = 0;
	struct dirent * kentry = readdir_fs(wd, index);
	while (kentry) {
		fs_printf(tty, "%s\n", kentry->name);

		index++;
		kentry = readdir_fs(wd, index);
	}
	close_fs(wd);
	free(wd);
	return 0;
}

static int shell_test_hash(fs_node_t * tty, int argc, char * argv[]) {

	fs_printf(tty, "Creating a hash...\n");

	hashmap_t * map = hashmap_create(2);

	hashmap_set(map, "a", (void *)1);
	hashmap_set(map, "b", (void *)2);
	hashmap_set(map, "c", (void *)3);

	fs_printf(tty, "value at a: %d\n", (int)hashmap_get(map, "a"));
	fs_printf(tty, "value at b: %d\n", (int)hashmap_get(map, "b"));
	fs_printf(tty, "value at c: %d\n", (int)hashmap_get(map, "c"));

	hashmap_set(map, "b", (void *)42);

	fs_printf(tty, "value at a: %d\n", (int)hashmap_get(map, "a"));
	fs_printf(tty, "value at b: %d\n", (int)hashmap_get(map, "b"));
	fs_printf(tty, "value at c: %d\n", (int)hashmap_get(map, "c"));

	hashmap_remove(map, "a");

	fs_printf(tty, "value at a: %d\n", (int)hashmap_get(map, "a"));
	fs_printf(tty, "value at b: %d\n", (int)hashmap_get(map, "b"));
	fs_printf(tty, "value at c: %d\n", (int)hashmap_get(map, "c"));
	fs_printf(tty, "map contains a: %s\n", hashmap_has(map, "a") ? "yes" : "no");
	fs_printf(tty, "map contains b: %s\n", hashmap_has(map, "b") ? "yes" : "no");
	fs_printf(tty, "map contains c: %s\n", hashmap_has(map, "c") ? "yes" : "no");

	list_t * hash_keys = hashmap_keys(map);
	foreach(_key, hash_keys) {
		char * key = (char *)_key->value;
		fs_printf(tty, "map[%s] = %d\n", key, (int)hashmap_get(map, key));
	}
	list_free(hash_keys);
	free(hash_keys);

	hashmap_free(map);
	free(map);

	return 0;
}

static int shell_log(fs_node_t * tty, int argc, char * argv[]) {
	if (argc < 2) {
		fs_printf(tty, "Log level is currently %d.\n", debug_level);
		fs_printf(tty, "Serial logging is %s.\n", kprint_to_serial ? "enabled" : "disabled");
		fs_printf(tty, "Usage: log [on|off] [<level>]\n");
	} else {
		if (!strcmp(argv[1], "on")) {
			kprint_to_serial = 1;
			if (argc > 2) {
				debug_level = atoi(argv[2]);
			}
		} else if (!strcmp(argv[1], "off")) {
			kprint_to_serial = 0;
		}
	}
	return 0;
}

static void dumb_sort(char * str) {
	int size = strlen(str);
	for (int i = 0; i < size-1; ++i) {
		for (int j = 0; j < size-1; ++j) {
			if (str[j] > str[j+1]) {
				char t = str[j+1];
				str[j+1] = str[j];
				str[j] = t;
			}
		}
	}
}

static int shell_anagrams(fs_node_t * tty, int argc, char * argv[]) {
	hashmap_t * map = hashmap_create(10);

	for (int i = 1; i < argc; ++i) {
		char * c = strdup(argv[i]);
		dumb_sort(c);

		list_t * l = hashmap_get(map, c);
		if (!l) {
			l = list_create();
			hashmap_set(map, c, l);
		}
		list_insert(l, argv[i]);

		free(c);
	}

	list_t * values = hashmap_values(map);
	foreach(val, values) {
		list_t * x = (list_t *)val->value;
		fs_printf(tty, "{");
		foreach(node, x) {
			fs_printf(tty, "%s", (char *)node->value);
			if (node->next) {
				fs_printf(tty, ", ");
			}
		}
		fs_printf(tty, "}%s", (!!val->next) ? ", " : "\n");
		free(x);
	}
	list_free(values);
	free(values);

	hashmap_free(map);
	free(map);

	return 0;
}

unsigned short pciConfigReadWord (unsigned short bus, unsigned short slot,
		unsigned short func, unsigned short offset)
{
	unsigned long address;
	unsigned long lbus = (unsigned long)bus;
	unsigned long lslot = (unsigned long)slot;
	unsigned long lfunc = (unsigned long)func;
	unsigned short tmp = 0;

	/* create configuration address as per Figure 1 */
	address = (unsigned long)((lbus << 16) | (lslot << 11) |
			(lfunc << 8) | (offset & 0xfc) | ((uint32_t)0x80000000));

	/* write out the address */
	outportl(0xCF8, address);
	/* read in the data */
	/* (offset & 2) * 8) = 0 will choose the fisrt word of the 32 bits register */
	tmp = (unsigned short)((inportl(0xCFC) >> ((offset & 2) * 8)) & 0xffff);
	return (tmp);
}

static void scan_hit_list(uint32_t device, uint16_t vendorid, uint16_t deviceid) {

	fs_node_t * tty = current_process->fds->entries[0];

	fs_printf(tty, "%x:%x.%x (%x, %x:%x) %s %s\n",
			(int)pci_extract_bus(device),
			(int)pci_extract_slot(device),
			(int)pci_extract_func(device),
			(int)pci_find_type(device),
			vendorid,
			deviceid,
			pci_vendor_lookup(vendorid),
			pci_device_lookup(vendorid,deviceid));

	fs_printf(tty, " BAR0: 0x%x\n", pci_read_field(device, PCI_BAR0, 4));
	fs_printf(tty, " BAR1: 0x%x\n", pci_read_field(device, PCI_BAR1, 4));
	fs_printf(tty, " BAR2: 0x%x\n", pci_read_field(device, PCI_BAR2, 4));
	fs_printf(tty, " BAR3: 0x%x\n", pci_read_field(device, PCI_BAR3, 4));
	fs_printf(tty, " BAR4: 0x%x\n", pci_read_field(device, PCI_BAR4, 4));
	fs_printf(tty, " BAR6: 0x%x\n", pci_read_field(device, PCI_BAR5, 4));

}

static int shell_pci(fs_node_t * tty, int argc, char * argv[]) {
	pci_scan(&scan_hit_list, -1);
	return 0;
}

static int shell_uid(fs_node_t * tty, int argc, char * argv[]) {
	if (argc < 2) {
		fs_printf(tty, "uid=%d\n", current_process->user);
	} else {
		current_process->user = atoi(argv[1]);
	}
	return 0;
}

typedef struct packet {
	fs_node_t * client_port; /* client "port"... it's actually the pointer to the pipe for the client. */
	pid_t       client_pid;  /* the pid of the client is always include because reasons */
	size_t      size;        /* size of the packet */
	uint8_t     data[];
} packet_t;

static void packet_send(fs_node_t * recver, fs_node_t * sender, size_t size, void * data) {
	size_t p_size = size + sizeof(struct packet);
	packet_t * p = malloc(p_size);
	memcpy(p->data, data, size);
	p->client_port = sender;
	p->client_pid  = current_process->id;
	p->size        = size;

	write_fs(recver, 0, p_size, (uint8_t *)p);

	free(p);
}

static void packet_recv(fs_node_t * socket, packet_t ** out) {
	packet_t tmp;
	read_fs(socket, 0, sizeof(struct packet), (uint8_t *)&tmp);
	*out = malloc(tmp.size + sizeof(struct packet));
	memcpy(*out, &tmp, sizeof(struct packet));
	read_fs(socket, 0, tmp.size, (uint8_t *)(*out)->data);
}

static void tasklet_client(void * data, char * name) {
	fs_node_t * server_pipe = (fs_node_t *)data;
	fs_node_t * client_pipe = make_pipe(4096);

	fs_node_t * tty = current_process->fds->entries[0];
	packet_send(server_pipe, client_pipe, strlen("Hello")+1, "Hello");

	while (1) {
		packet_t * p;
		packet_recv(client_pipe, &p);
		fs_printf(tty, "Client %s Received: %s\n", name, (char *)p->data);
		if (!strcmp((char*)p->data, "PING")) {
			packet_send(server_pipe, client_pipe, strlen("PONG")+1, "PONG");
		}
		free(p);
	}
}

static int shell_server_running = 0;
static fs_node_t * shell_server_node = NULL;

static void tasklet_server(void * data, char * name) {
	fs_node_t * tty = current_process->fds->entries[0];
	fs_node_t * socket = make_pipe(4096);

	shell_server_node = socket;

	create_kernel_tasklet(tasklet_client, "ktty-client-1", socket);
	create_kernel_tasklet(tasklet_client, "ktty-client-2", socket);
	create_kernel_tasklet(tasklet_client, "ktty-client-3", socket);

	fs_printf(tty, "Going to perform a quick demo...\n");

	int i = 0;
	fs_node_t * outputs[3];
	while (i < 3) {
		packet_t * p;
		packet_recv(socket, &p);
		fs_printf(tty, "Server received %s from %d:%d\n", (char*)p->data, p->client_pid, p->client_port);
		packet_send(p->client_port, socket, strlen("Welcome!")+1, "Welcome!");
		outputs[i] = p->client_port;
		free(p);
		i++;
	}

	fs_printf(tty, "Okay, that's everyone, time to send some responses.\n");
	i = 0;
	while (i < 3) {
		packet_send(outputs[i], socket, strlen("PING")+1, "PING");
		i++;
	}

	i = 0;
	while (i < 3) {
		packet_t * p;
		packet_recv(socket, &p);
		fs_printf(tty, "PONG from %d\n", p->client_pid);
		free(p);
		i++;
	}

	fs_printf(tty, "And that's the demo of packet servers.\n");
	fs_printf(tty, "Now running in echo mode, will respond to all clients with whatever they sent.\n");

	while (1) {
		packet_t * p;
		packet_recv(socket, &p);
		packet_send(p->client_port, socket, p->size, p->data);
		free(p);
	}
}

static int shell_server_test(fs_node_t * tty, int argc, char * argv[]) {
	if (!shell_server_running) {
		shell_server_running = 1;
		create_kernel_tasklet(tasklet_server, "ktty-server", NULL);
		fs_printf(tty, "Started server.\n");
	}

	return 0;
}

static int shell_client_test(fs_node_t * tty, int argc, char * argv[]) {
	if (!shell_server_running) {
		fs_printf(tty, "No server running, won't be able to connect.\n");
		return 1;
	}
	if (argc < 2) {
		fs_printf(tty, "expected argument\n");
		return 1;
	}

	fs_node_t * client_pipe = make_pipe(4096);

	packet_send(shell_server_node, client_pipe, strlen(argv[1])+1, argv[1]);

	while (1) {
		packet_t * p;
		packet_recv(client_pipe, &p);
		fs_printf(tty, "Got response from server: %s\n", (char *)p->data);
		free(p);
		break;
	}

	close_fs(client_pipe);

	return 0;
}

static struct shell_command shell_commands[] = {
	{"shell", &shell_create_userspace_shell,
		"Runs a userspace shell on this tty."},
	{"echo",  &shell_echo,
		"Prints arguments."},
	{"help",  &shell_help,
		"Prints a list of possible shell commands and their descriptions."},
	{"cd",    &shell_cd,
		"Change current directory."},
	{"ls",    &shell_ls,
		"List files in current or other directory."},
	{"test-hash", &shell_test_hash,
		"Test hashmap functionality."},
	{"log", &shell_log,
		"Configure serial debug logging."},
	{"anagrams", &shell_anagrams,
		"Demo of hashmaps and lists. Give a list of words, get a grouping of anagrams."},
	{"pci", &shell_pci,
		"Print PCI devices, as well as their names and BARs."},
	{"uid", &shell_uid,
		"Change the effective user id of the shell (useful when running `shell`)."},
	{"server-test", &shell_server_test,
		"Spawn a packet server and some clients."},
	{"client-test", &shell_client_test,
		"Communicate with packet server."},
	{NULL, NULL, NULL}
};

/*
 * A TTY object to pass to the tasklets for handling
 * serial-tty interaction. This probably shouldn't
 * be done as tasklets - TTYs should just be able
 * to wrap existing fs_nodes themselves, but that's
 * a problem for another day.
 */
struct tty_o {
	fs_node_t * node;
	fs_node_t * tty;
};

/*
 * These tasklets handle tty-serial interaction.
 */
void debug_shell_handle_in(void * data, char * name) {
	struct tty_o * tty = (struct tty_o *)data;
	while (1) {
		uint8_t buf[1];
		int r = read_fs(tty->tty, 0, 1, (unsigned char *)buf);
		write_fs(tty->node, 0, r, buf);
	}
}

void debug_shell_handle_out(void * data, char * name) {
	struct tty_o * tty = (struct tty_o *)data;
	while (1) {
		uint8_t buf[1];
		int r = read_fs(tty->node, 0, 1, (unsigned char *)buf);
		write_fs(tty->tty, 0, r, buf);
	}
}

/*
 * Determine the size of a smart terminal that we don't have direct
 * termios access to. This is done by sending a cursor-move command
 * that will put the cursor into the lower right corner and then
 * requesting the cursor position report. We then read and parse
 * the position report. In the case where the terminal on the other
 * end is actually dumb, we end up waiting for some input and
 * then timing out.
 * TODO with asyncio support, the timeout should actually work.
 *      consider also using an alarm (which I also don't have)
 */
void divine_size(fs_node_t * dev, int * width, int * height) {
	char tmp[100];
	int read = 0;
	unsigned long start_tick = timer_ticks;
	/* Move cursor, Request position, Reset cursor */
	fs_printf(dev, "\033[1000;1000H\033[6n\033[H");
	while (1) {
		char buf[1];
		int r = read_fs(dev, 0, 1, (unsigned char *)buf);
		if (r > 0) {
			if (buf[0] != 'R') {
				if (read > 1) {
					tmp[read-2] = buf[0];
				}
				read++;
			} else {
				break;
			}
		}
		if (timer_ticks - start_tick >= 2) {
			/*
			 * We've timed out. This will only be triggered
			 * when we eventually receive something, though
			 */
			*width  = 80;
			*height = 23;
			/* Clear and return */
			fs_printf(dev, "\033[J");
			return;
		}
	}
	/* Clear */
	fs_printf(dev, "\033[J");
	/* Break up the result into two strings */
	for (unsigned int i = 0; i < strlen(tmp); i++) {
		if (tmp[i] == ';') {
			tmp[i] = '\0';
			break;
		}
	}
	char * h = (char *)((uintptr_t)tmp + strlen(tmp)+1);
	/* And then parse it into numbers */
	*height = atoi(tmp);
	*width  = atoi(h);
}

/*
 * Tasklet for managing the kernel serial console.
 * This is basically a very simple shell, with access
 * to some internal kernel commands, and (eventually)
 * debugging routines.
 */
void debug_shell_run(void * data, char * name) {
	/*
	 * We will run on the first serial port.
	 * TODO detect that this failed
	 */
	fs_node_t * tty = kopen("/dev/ttyS0", 0);

	/* Our prompt will include the version number of the current kernel */
	char version_number[1024];
	sprintf(version_number, __kernel_version_format,
			__kernel_version_major,
			__kernel_version_minor,
			__kernel_version_lower,
			__kernel_version_suffix);

	/* We will convert the serial interface into an actual TTY */
	int master, slave;
	struct winsize size = {0,0,0,0};

	/* Attempt to divine the terminal size. Changing the window size after this will do bad things */
	int width, height;
	divine_size(tty, &width, &height);

	size.ws_row = height;
	size.ws_col = width;

	/* Convert the serial line into a TTY */
	openpty(&master, &slave, NULL, NULL, &size);

	/* Attach the serial to the TTY interface */
	struct tty_o _tty = {.node = current_process->fds->entries[master], .tty = tty};

	create_kernel_tasklet(debug_shell_handle_in,  "[kttydebug-in]",  (void *)&_tty);
	create_kernel_tasklet(debug_shell_handle_out, "[kttydebug-out]", (void *)&_tty);

	/* Set the device to be the actual TTY slave */
	tty = current_process->fds->entries[slave];

	current_process->fds->entries[0] = tty;
	current_process->fds->entries[1] = tty;
	current_process->fds->entries[2] = tty;

	/* Initialize the shell commands map */
	if (!shell_commands_map) {
		shell_commands_map = hashmap_create(10);
		struct shell_command * sh = &shell_commands[0];
		while (sh->name) {
			hashmap_set(shell_commands_map, sh->name, sh);
			sh++;
		}
	}

	int retval = 0;

	while (1) {
		char command[512];

		/* Print out the prompt */
		if (retval) {
			fs_printf(tty, "%s-%s %d %s# ", __kernel_name, version_number, retval, current_process->wd_name);
		} else {
			fs_printf(tty, "%s-%s %s# ", __kernel_name, version_number, current_process->wd_name);
		}

		/* Read a line */
		debug_shell_readline(tty, command, 511);

		char * arg = strdup(command);
		char * argv[1024];  /* Command tokens (space-separated elements) */
		int argc = tokenize(arg, " ", argv);

		if (!argc) continue;

		/* Parse the command string */
		struct shell_command * sh = hashmap_get(shell_commands_map, argv[0]);
		if (sh) {
			retval = sh->function(tty, argc, argv);
		} else {
			fs_printf(tty, "Unrecognized command: %s\n", argv[0]);
		}

		free(arg);
	}
}

int debug_shell_start(void) {
	int i = create_kernel_tasklet(debug_shell_run, "[kttydebug]", NULL);
	debug_print(NOTICE, "Started tasklet with pid=%d", i);

	return 0;
}
