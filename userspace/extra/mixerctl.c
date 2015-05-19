/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2015 Mike Gerow
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "lib/sound.h"

static const char usage[] =
"Usage  %s [-d device_id] -l\n"
"       %s [-d device_id] [-k knob_id] -r\n"
"       %s [-d device_id] [-k knob_id] -w knob_value\n"
"       %s -h\n"
"    -d: Device id to address. Defaults to the main sound device.\n"
"    -l: List the knobs on a device.\n"
"    -k: Knob id to address. Defaults to the device's master knob.\n"
"    -r: Perform a read on the given device's knob. Defaults to the device's\n"
"        master knob.\n"
"    -w: Perform a write on the given device's knob. The value should be a\n"
"        float from 0.0 to 1.0.\n"
"    -h: Print this help message and exit.\n";

int main(int argc, char * argv[]) {
	uint32_t device_id = SND_DEVICE_MAIN;
	uint32_t knob_id   = SND_KNOB_MASTER;
	uint8_t list_flag = 0;
	uint8_t read_flag  = 0;
	uint8_t write_flag = 0;
	double write_value = 0.0;

	int c;

	while ((c = getopt(argc, argv, "d:lk:rw:h")) != -1) {
		switch (c) {
			case 'd':
				device_id = atol(optarg);
				break;
			case 'l':
				list_flag = 1;
				break;
			case 'k':
				knob_id = atol(optarg);
				break;
			case 'r':
				read_flag = 1;
				break;
			case 'w':
				write_flag = 1;
				write_value = atof(optarg);
				if (write_value < 0.0 || write_value > 1.0) {
					fprintf(stderr, "argument -w value must be between 0.0 and 1.0\n");
					exit(EXIT_FAILURE);
				}
				break;
			case 'h':
				fprintf(stdout, usage, argv[0], argv[0], argv[0], argv[0]);
				exit(EXIT_SUCCESS);
			default:
				fprintf(stderr, usage, argv[0], argv[0], argv[0], argv[0]);
				exit(EXIT_FAILURE);
		}
	}

	int mixer = open("/dev/mixer", O_RDONLY);
	if (mixer < 1) {
		perror("open");
		exit(EXIT_FAILURE);
	}

	if (list_flag) {
		snd_knob_list_t list = {0};
		list.device = device_id;
		if (ioctl(mixer, SND_MIXER_GET_KNOBS, &list) < 0) {
			perror("ioctl");
			exit(EXIT_FAILURE);
		}
		for (uint32_t i = 0; i < list.num; i++) {
			snd_knob_info_t info = {0};
			info.device = device_id;
			info.id = list.ids[i];
			if (ioctl(mixer, SND_MIXER_GET_KNOB_INFO, &info) < 0) {
				perror("ioctl");
				exit(EXIT_FAILURE);
			}
			fprintf(stdout, "%d: %s\n", info.id, info.name);
		}

		exit(EXIT_SUCCESS);
	}

	if (read_flag) {
		snd_knob_value_t value = {0};
		value.device = device_id;
		value.id = knob_id;
		if (ioctl(mixer, SND_MIXER_READ_KNOB, &value) < 0) {
			perror("ioctl");
			exit(EXIT_FAILURE);
		}
		double double_val = (double)value.val / SND_KNOB_MAX_VALUE;
		fprintf(stdout, "%f\n", double_val);
		exit(EXIT_FAILURE);
	}

	if (write_flag) {
		snd_knob_value_t value = {0};
		value.device = device_id;
		value.id = knob_id;
		value.val = (uint32_t)(write_value * SND_KNOB_MAX_VALUE);
		if (ioctl(mixer, SND_MIXER_WRITE_KNOB, &value) < 0) {
			perror("ioctl");
			exit(EXIT_FAILURE);
		}
		exit(EXIT_SUCCESS);
	}

	fprintf(stderr, "No operation specified.\n");
	exit(EXIT_FAILURE);
}
