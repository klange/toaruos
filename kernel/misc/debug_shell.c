/*
 * Kernel Debug Shell
 */
#include <system.h>
#include <fs.h>
#include <logging.h>
#include <process.h>
#include <version.h>
#include <termios.h>

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


void fs_printf(fs_node_t * device, char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	char buffer[1024];
	vasprintf(buffer, fmt, args);
	va_end(args);

	write_fs(device, 0, strlen(buffer), (uint8_t *)buffer);
}

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

void debug_shell_run_sh(void * data, char * name) {

	fs_node_t * tty = (fs_node_t *)data;

	current_process->fds->entries[0] = tty;
	current_process->fds->entries[1] = tty;
	current_process->fds->entries[2] = tty;

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

struct tty_o {
	fs_node_t * node;
	fs_node_t * tty;
};

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

void divine_size(fs_node_t * dev, int * width, int * height) {
	char tmp[100];
	int read = 0;
	unsigned long start_tick = timer_ticks;
	fs_printf(dev, "\033[1000;1000H\033[6n\033[H\033[J");
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
			*width  = 80;
			*height = 23;
			return;
		}
	}
	for (unsigned int i = 0; i < strlen(tmp); i++) {
		if (tmp[i] == ';') {
			tmp[i] = '\0';
			break;
		}
	}
	char * h = (char *)((uintptr_t)tmp + strlen(tmp)+1);
	*height = atoi(tmp);
	*width  = atoi(h);
}

void debug_shell_run(void * data, char * name) {
	fs_node_t * tty = kopen("/dev/ttyS0", 0);
	char version_number[1024];
	sprintf(version_number, __kernel_version_format,
			__kernel_version_major,
			__kernel_version_minor,
			__kernel_version_lower,
			__kernel_version_suffix);

	int master, slave;
	struct winsize size = {0,0,0,0};

	/* Attempt to divine the terminal size. Changing the window size after this will do bad things */
	int width, height;
	divine_size(tty, &width, &height);

	size.ws_row = height;
	size.ws_col = width;

	openpty(&master, &slave, NULL, NULL, &size);

	struct tty_o _tty = {.node = current_process->fds->entries[master], .tty = tty};

	create_kernel_tasklet(debug_shell_handle_in,  "[kttydebug-in]",  (void *)&_tty);
	create_kernel_tasklet(debug_shell_handle_out, "[kttydebug-out]", (void *)&_tty);

	/* Set the device to be the actual TTY slave */
	tty = current_process->fds->entries[slave];

	while (1) {
		char command[512];

		/* Print out the prompt */
		fs_printf(tty, "%s-%s %s# ", __kernel_name, version_number, current_process->wd_name);

		/* Read a line */
		debug_shell_readline(tty, command, 511);

		/* Do something with it */
		char * arg = strdup(command);
		char * pch;         /* Tokenizer pointer */
		char * save;        /* We use the reentrant form of strtok */
		char * argv[1024];  /* Command tokens (space-separated elements) */
		int    tokenid = 0; /* argc, basically */

		/* Tokenize the arguments, splitting at spaces */
		pch = strtok_r(arg," ",&save);
		if (!pch) {
			free(arg);
			continue;
		}
		while (pch != NULL) {
			argv[tokenid] = (char *)pch;
			++tokenid;
			pch = strtok_r(NULL," ",&save);
		}
		argv[tokenid] = NULL;
		/* Tokens are now stored in argv. */

		if (!strcmp(argv[0], "shell")) {
			int pid = create_kernel_tasklet(debug_shell_run_sh, "[[k-sh]]", tty);
			fs_printf(tty, "Shell started with pid = %d\n", pid);
			process_t * child_task = process_from_pid(pid);
			sleep_on(child_task->wait_queue);
			fs_printf(tty, "Shell returned: %d\n", child_task->status);
		} else if (!strcmp(argv[0], "echo")) {
			for (int i = 1; i < tokenid; ++i) {
				fs_printf(tty, "%s ", argv[i]);
			}
			fs_printf(tty, "\n");
		} else if (!strcmp(argv[0], "setuid")) {
			current_process->user = atoi(argv[1]);
		}

		free(arg);
	}
}

int debug_shell_start(void) {
	int i = create_kernel_tasklet(debug_shell_run, "[kttydebug]", NULL);
	debug_print(NOTICE, "Started tasklet with pid=%d", i);

	return 0;
}
