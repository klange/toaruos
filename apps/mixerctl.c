/**
 * @brief Control audio mixer knobs
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2015 Mike Gerow
 * Copyright (C) 2026 K. Lange
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>

#include <kernel/mod/sound.h>

enum operations {
	OP_LIST,
	OP_READ,
	OP_WRITE,
};

static int show_usage(char *argv[]) {
#define X_S "\033[3m"
#define X_E "\033[0m"
	fprintf(stderr,
		"%s - Control audio mixer settings.\n"
		"\n"
		"Usage  %s [-m mixer] [-d device_id] [-l]\n"
		"       %s [-m mixer] [-d device_id] [-k knob_id] -r\n"
		"       %s [-m mixer] [-d device_id] [-k knob_id] -w knob_value\n"
		"       %s -h\n"
		"\n"
		" -d " X_S "device_id    Device id to address. Defaults to the main sound device." X_E "\n"
		" -l              " X_S "List the knobs on a device." X_E "\n"
		" -k " X_S "knob_id      Knob id to address. Defaults to the device's master knob." X_E "\n"
		" -r              " X_S "Perform a read on the given device's knob. Defaults to" X_E "\n"
		"                 " X_S "the device's master knob." X_E "\n"
		" -w " X_S "knob_value   Perform a write on the given device's knob. The value" X_E "\n"
		"                 " X_S "should be a float from 0.0 to 1.0." X_E "\n"
		" -m " X_S "mixer        Use an alternative mixer device file." X_E "\n"
		" -h              " X_S "Print this help message and exit." X_E "\n",
		argv[0], argv[0], argv[0], argv[0], argv[0]);
	return 2;
}

static int snd_error(int err, char * argv0, char * mixer_path, uint32_t device_id, uint32_t knob_id) {
	if (err == ENODEV)      fprintf(stderr, "%s: no audio device at id '%u'\n", argv0, device_id);
	else if (err == ENXIO)  fprintf(stderr, "%s: device %u has no knob at id '%u'\n", argv0, knob_id, device_id);
	else                    fprintf(stderr, "%s: %s: %s\n", argv0, mixer_path, strerror(err));
	return 1;
}

int main(int argc, char * argv[]) {
	uint32_t device_id = SND_DEVICE_MAIN;
	uint32_t knob_id   = SND_KNOB_MASTER;
	double write_value = 0.0;
	char * mixer_path = "/dev/mixer";

	enum operations operation = OP_LIST;

	int c;

	while ((c = getopt(argc, argv, "d:lk:rw:m:h?")) != -1) {
		switch (c) {
			case 'd':
				device_id = atoi(optarg);
				break;
			case 'l':
				if (operation) return show_usage(argv);
				operation = OP_LIST;
				break;
			case 'k':
				knob_id = atoi(optarg);
				break;
			case 'r':
				if (operation) return show_usage(argv);
				operation = OP_READ;
				break;
			case 'w':
				if (operation) return show_usage(argv);
				operation = OP_WRITE;
				write_value = atof(optarg);
				if (write_value < 0.0 || write_value > 1.0) {
					fprintf(stderr, "argument -w value must be between 0.0 and 1.0\n");
					return 2;
				}
				break;
			case 'm':
				mixer_path = optarg;
				break;
			case 'h':
			case '?':
			default:
				return show_usage(argv);
		}
	}

	int mixer = open(mixer_path, O_RDONLY);
	if (mixer < 1) {
		fprintf(stderr, "%s: %s: %s\n", argv[0], mixer_path, strerror(errno));
		return 1;
	}

	switch (operation) {
		case OP_LIST: {
			snd_knob_list_t list = {0};
			list.device = device_id;
			if (ioctl(mixer, SND_MIXER_GET_KNOBS, &list) < 0) return snd_error(errno, argv[0], mixer_path, device_id, 0);
			for (uint32_t i = 0; i < list.num; i++) {
				snd_knob_info_t info = {0};
				info.device = device_id;
				info.id = list.ids[i];
				if (ioctl(mixer, SND_MIXER_GET_KNOB_INFO, &info) < 0) return snd_error(errno, argv[0], mixer_path, device_id, 0);
				fprintf(stdout, "%d: %s\n", (unsigned int)info.id, info.name);
			}
			return 0;
		}

		case OP_READ: {
			snd_knob_value_t value = {0};
			value.device = device_id;
			value.id = knob_id;
			if (ioctl(mixer, SND_MIXER_READ_KNOB, &value) < 0) return snd_error(errno, argv[0], mixer_path, device_id, knob_id);
			double double_val = (double)value.val / SND_KNOB_MAX_VALUE;
			fprintf(stdout, "%f\n", double_val);
			return 0;
		}

		case OP_WRITE: {
			snd_knob_value_t value = {0};
			value.device = device_id;
			value.id = knob_id;
			value.val = (uint32_t)(write_value * SND_KNOB_MAX_VALUE);
			if (ioctl(mixer, SND_MIXER_WRITE_KNOB, &value) < 0) return snd_error(errno, argv[0], mixer_path, device_id, knob_id);
			return 0;
		}

		default:
			return show_usage(argv);
	}
}
