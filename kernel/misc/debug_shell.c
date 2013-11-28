/*
 * Kernel Debug Shell
 */
#include <system.h>
#include <fs.h>
#include <logging.h>
#include <process.h>
#include <version.h>

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

void debug_shell_run(void * data, char * name) {
	fs_node_t * tty = kopen("/dev/ttyS0", 0);
	char version_number[1024];
	sprintf(version_number, __kernel_version_format,
			__kernel_version_major,
			__kernel_version_minor,
			__kernel_version_lower,
			__kernel_version_suffix);
	while (1) {
		char command[512];

		/* Print out the prompt */
		fs_printf(tty, "%s-%s %s# ", __kernel_name, version_number, current_process->wd_name);

		/* Read a line */
		int r = debug_shell_readline(tty, command, 511);

		/* Do something with it */
		if (!strcmp(command, "shell")) {
			int pid = create_kernel_tasklet(debug_shell_run_sh, "[[k-sh]]", tty);
			fs_printf(tty, "Shell started with pid = %d\n", pid);
			process_t * child_task = process_from_pid(pid);
			sleep_on(child_task->wait_queue);
			fs_printf(tty, "Shell returned: %d\n", child_task->status);
		}
	}
}

int debug_shell_start(void) {
	int i = create_kernel_tasklet(debug_shell_run, "[kttydebug]", NULL);
	debug_print(NOTICE, "Started tasklet with pid=%d", i);

	return 0;
}
