#include <system.h>
#include <logging.h>
#include <args.h>
#include <tokenize.h>
#include <ata.h>

/* XXX: This should be moved */
void ext2_disk_mount(uint32_t offset_sector, uint32_t max_sector);
int read_partition_map(int device);

void legacy_parse_args(void) {
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

	if (args_present("hdd")) {
		ide_init(0x1F0);
		char * c = args_value("hdd");
		if (!c) {
			ext2_disk_mount(0,0);
		} else {
			if (read_partition_map(0)) {
				debug_print(ERROR, "Failed to read MBR.");
			} else {
				int partition = atoi(c);
				debug_print(NOTICE, "Selected partition %d starts at sector %d", partition, mbr.partitions[partition].lba_first_sector);
				ext2_disk_mount(mbr.partitions[partition].lba_first_sector,mbr.partitions[partition].sector_count);
			}
		}
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

	if ((c = args_value("logtoserial"))) {
		kprint_to_serial = 1;
		debug_level = atoi(c);
		debug_print(NOTICE, "Kernel serial logging enabled at level %d.", debug_level);
	}

	if ((c = args_value("kernel-term"))) {
		kprint_to_screen = 1;
		debug_level = atoi(c);
		debug_print(NOTICE, "Kernel screen logging enabled at level %d.", debug_level);
	}

	if (args_present("read-mbr")) {
		read_partition_map(0);
	}
}

