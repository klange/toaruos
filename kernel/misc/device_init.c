#include <system.h>
#include <logging.h>
#include <args.h>
#include <tokenize.h>
#include <fs.h>

/* XXX: This should be moved */
void ext2_disk_mount(fs_node_t *);
int read_partition_map(fs_node_t *);

void early_stage_args(void) {
	char * c;

	if ((c = args_value("vid"))) {
		debug_print(NOTICE, "Video mode requested: %s", c);

		char * arg = strdup(c);
		char * argv[10];
		int argc = tokenize(arg, ",", argv);

		uint16_t x, y;
		if (argc < 3) {
			x = 1024;
			y = 768;
		} else {
			x = atoi(argv[1]);
			y = atoi(argv[2]);
		}

		if (!strcmp(argv[0], "qemu")) {
			/* Bochs / Qemu Video Device */
			graphics_install_bochs(x,y);
		} else if (!strcmp(argv[0],"preset")) {
			graphics_install_preset(x,y);
		} else {
			debug_print(WARNING, "Unrecognized video adapter: %s", argv[0]);
		}

		free(arg);
	}

	if (args_present("single")) {
		boot_arg = "--single";
	} else if (args_present("lite")) {
		boot_arg = "--special";
	} else if (args_present("vgaterm")) {
		boot_arg = "--vga";
	} else if (args_present("start")) {
		char * c = args_value("start");
		if (!c) {
			debug_print(WARNING, "Expected an argument to kernel option `start`. Ignoring.");
		} else {
			boot_arg_extra = c;
		}
	}

	if (args_present("read-mbr")) {
		fs_node_t * f = kopen(args_value("root"), 0);
		read_partition_map(f);
	}
}

void late_stage_args(void) {
	/* Nothing to do here */
}
