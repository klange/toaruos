/* vim: tabstop=4 shiftwidth=4 noexpandtab
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 *
 * lspci - Print information about connected PCI devices.
 *
 */
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

struct device_class {
	char * code;
	char * name;
} device_classes[] = {
	{"0101", "IDE interface"},
	{"0102", "Floppy disk controller"},
	{"0105", "ATA controller"},
	{"0106", "SATA controller"},
	{"0200", "Ethernet controller"},
	{"0280", "Network controller"},
	{"0300", "VGA compatible controller"},
	{"0380", "Display controller"},
	{"0401", "Multimedia audio controller"},
	{"0403", "Audio device"},
	{"0480", "Multimedia controller"},
	{"0600", "Host bridge"},
	{"0601", "ISA bridge"},
	{"0680", "Bridge"},
	{"0880", "System peripheral"},
	{NULL, NULL},
};

static void show_usage(char * argv[]) {
	fprintf(stderr,
			"lspci - show information about PCI devices\n"
			"\n"
			"usage: %s [-n]\n"
			"\n"
			" -n     \033[3mshow numeric device codes\033[0m\n"
			" -?     \033[3mshow this help text\033[0m\n"
			"\n", argv[0]);
}

int main(int argc, char * argv[]) {
	int numeric = 0;
	int opt;
	while ((opt = getopt(argc, argv, "n?")) != -1) {
		switch (opt) {
			case '?':
				show_usage(argv);
				return 0;
			case 'n':
				numeric = 1;
				break;
		}
	}

	FILE * f = fopen("/proc/pci","r");
	if (!f) {
		fprintf(stderr, "%s: %s: %s\n", argv[0], "/proc/pci", strerror(errno));
		return 1;
	}

	while (!feof(f)) {
		char line[1024];
		fgets(line, 1024, f);
		if (line[0] == ' ') {
			/* Skip; don't care about this information */
			continue;
		}
		/* Read bus, etc. verbatim */
		char * device_bus   = line;

		/* Read device class */
		char * device_class = strstr(line," (");
		if (!device_class) {
			fprintf(stderr, "%s: parse error\n", argv[0]);
			return 1;
		}
		*device_class = '\0';
		device_class++; /* space */
		device_class++; /* ( */

		char * device_vendor = strstr(device_class, ", ");
		if (!device_vendor) {
			fprintf(stderr, "%s: parse error\n", argv[0]);
			return 1;
		}
		*device_vendor = '\0';
		device_vendor++; /* comma */
		device_vendor++; /* space */

		char * device_code = strstr(device_vendor, ":");
		if (!device_code) {
			fprintf(stderr, "%s: parse error\n", argv[0]);
			return 1;
		}
		*device_code = '\0';
		device_code++; /* colon */

		char * device_name = strstr(device_code, ") ");
		if (!device_name) {
			fprintf(stderr, "%s: parse error\n", argv[0]);
			return 1;
		}
		*device_name = '\0';
		device_name++; /* ) */
		device_name++; /* space */

		char * linefeed = strstr(device_name, "\n");
		if (linefeed) *linefeed = '\0';

		if (numeric) {
			fprintf(stdout, "%s %s: %s:%s\n", device_bus, device_class, device_vendor, device_code);
		} else {
			for (struct device_class * c = device_classes; c->code; ++c) {
				if (!strcmp(device_class, c->code)) {
					device_class = c->name;
					break;
				}
			}
			/* TODO: We should also look up vendor + device names ourselves and possibly remove them from the kernel */
			fprintf(stdout, "%s %s: %s\n", device_bus, device_class, device_name);
		}
	}

	return 0;
}
