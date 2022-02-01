/**
 * lspci - Print information about connected PCI devices.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 */
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

struct device_class {
	uint16_t id;
	char * name;
} _pci_classes[] = {
	{0x0101, "IDE interface"},
	{0x0102, "Floppy disk controller"},
	{0x0105, "ATA controller"},
	{0x0106, "SATA controller"},
	{0x0200, "Ethernet controller"},
	{0x0280, "Network controller"},
	{0x0300, "VGA compatible controller"},
	{0x0380, "Display controller"},
	{0x0401, "Multimedia audio controller"},
	{0x0403, "Audio device"},
	{0x0480, "Multimedia controller"},
	{0x0600, "Host bridge"},
	{0x0601, "ISA bridge"},
	{0x0604, "PCI bridge"},
	{0x0680, "Bridge"},
	{0x0780, "Communication controller"},
	{0x0805, "SD Host controller"},
	{0x0880, "System peripheral"},
	{0x0900, "Keyboard"},
	{0x0980, "Input Device"},
	{0x0c00, "FireWire controller"},
	{0x0c03, "USB controller"},
	{0x0c05, "SMBus controller"},
	{0x1180, "Signal processing controller"},
};

struct {
	uint16_t id;
	const char * name;
} _pci_vendors[] = {
	{0x1013, "Cirrus Logic"},
	{0x1022, "AMD"},
	{0x106b, "Apple, Inc."},
	{0x10de, "NVIDIA Corp."},
	{0x1180, "Ricoh Ct. Ltd."},
	{0x1234, "Bochs/QEMU"},
	{0x1274, "Ensoniq"},
	{0x15ad, "VMWare"},
	{0x1912, "Renesas Electronics Corp."}, /* Formerly "Renesas Technology Corp." */
	{0x1af4, "Red Hat, Inc."}, /* virtio */
	{0x1b36, "Red Hat, Inc."},
	{0x8086, "Intel Corporation"},
	{0x80EE, "VirtualBox"},
};

struct {
	uint16_t ven_id;
	uint16_t dev_id;
	const char * name;
} _pci_devices[] = {
	{0x1013, 0x00b8, "CLGD 54xx VGA Adapter"},
	{0x1022, 0x2000, "PCNet Ethernet Controller (pcnet)"},
	{0x106b, 0x003f, "OHCI Controller"},
	{0x10de, 0x0a6c, "Quadro NVS 3100M"},

	/* Ricoh */
	{0x1180, 0xe822, "MMC/SD Host Controller"},
	{0x1180, 0xe230, "R5U2xx Memory Stick Host Controller"},
	{0x1180, 0xe832, "R5C832 PCIe IEEE 1394 Controller"},

	{0x1234, 0x1111, "VGA BIOS Graphics Extensions"},
	{0x1274, 0x1371, "Creative Labs CT2518 (ensoniq audio)"},

	/* VMWare */
	{0x15ad, 0x0740, "VM Communication Interface"},
	{0x15ad, 0x0405, "SVGA II Adapter"},
	{0x15ad, 0x0790, "PCI bridge"},
	{0x15ad, 0x07a0, "PCI Express Root Port"},

	/* Renesas */
	{0x1912, 0x0015, "uPD720202 USB 3.0 Host Controller"},

	/* Red Hat */
	{0x1af4, 0x1000, "virtio-net"},
	{0x1af4, 0x1052, "virtio-input"},
	{0x1b36, 0x000d, "QEMU XHCI Host Controller"},

	/* Intel */
	{0x8086, 0x0044, "DRAM Controller"},
	{0x8086, 0x0045, "PCI Express x16 Root Port"},
	{0x8086, 0x0046, "Gen 5 HD Graphics"},
	{0x8086, 0x1004, "82543GC Gigabit Ethernet Controller (e1000)"},
	{0x8086, 0x100e, "82540EM Gigabit Ethernet Controller (e1000)"},
	{0x8086, 0x100f, "82545EM Gigabit Ethernet Controller (e1000)"},
	{0x8086, 0x10d3, "82574L Gigabit Ethernet Controller (e1000e)"},
	{0x8086, 0x10ea, "82577LM Gigabit Ethernet Controller (e1000)"},
	{0x8086, 0x1237, "PCI & Memory"},
	{0x8086, 0x2415, "82801AA AC'97 Audio Controller"},
	{0x8086, 0x2448, "82801 Mobile PCI Bridge"},
	{0x8086, 0x2668, "ICH6 HD Audio Controller"},
	{0x8086, 0x29c0, "DRAM Controller"},
	{0x8086, 0x2918, "ICH9 LPC Interface Controller"},
	{0x8086, 0x2922, "ICH9 6-port SATA Controller"},
	{0x8086, 0x2930, "ICH9 SMBus Controller"},
	{0x8086, 0x3b07, "QM57 Chipset LPC Interface Controller"},
	{0x8086, 0x3b2f, "ICH10 6-port SATA AHCI Controller"},
	{0x8086, 0x3b30, "ICH10 SMBus Controller"},
	{0x8086, 0x3b32, "ICH10 Thermal Subsystem"},
	{0x8086, 0x3b34, "ICH10 USB 2.0 Enhanced Host Controller"},
	{0x8086, 0x3b3c, "ICH10 USB 2.0 Enhanced Host Controller"},
	{0x8086, 0x3b42, "ICH10 PCI Express Root Port 1"},
	{0x8086, 0x3b44, "ICH10 PCI Express Root Port 2"},
	{0x8086, 0x3b46, "ICH10 PCI Express Root Port 3"},
	{0x8086, 0x3b48, "ICH10 PCI Express Root Port 4"},
	{0x8086, 0x3b4a, "ICH10 PCI Express Root Port 5"},
	{0x8086, 0x3b4c, "ICH10 PCI Express Root Port 6"},
	{0x8086, 0x3b4e, "ICH10 PCI Express Root Port 7"},
	{0x8086, 0x3b50, "ICH10 PCI Express Root Port 8"},
	{0x8086, 0x3b56, "ICH10 HD Audio Controller"},
	{0x8086, 0x3b64, "ICH10 HECI Controller"},
	{0x8086, 0x422b, "Centrino Ultimate-N 6300"},
	{0x8086, 0x7000, "PCI-to-ISA Bridge"},
	{0x8086, 0x7010, "IDE Interface"},
	{0x8086, 0x7110, "PIIX4 ISA"},
	{0x8086, 0x7111, "PIIX4 IDE"},
	{0x8086, 0x7113, "Power Management Controller"},
	{0x8086, 0x7190, "Host Bridge"},
	{0x8086, 0x7191, "AGP Bridge"},

	/* VirtualBox */
	{0x80EE, 0xBEEF, "Bochs/QEMU-compatible Graphics Adapter"},
	{0x80EE, 0xCAFE, "Guest Additions Device"},
};

const char * pci_class_lookup(unsigned short class_id) {
	for (unsigned int i = 0; i < sizeof(_pci_classes)/sizeof(_pci_classes[0]); ++i) {
		if (_pci_classes[i].id == class_id) {
			return _pci_classes[i].name;
		}
	}
	return "(unknown)";
}

const char * pci_vendor_lookup(unsigned short vendor_id) {
	for (unsigned int i = 0; i < sizeof(_pci_vendors)/sizeof(_pci_vendors[0]); ++i) {
		if (_pci_vendors[i].id == vendor_id) {
			return _pci_vendors[i].name;
		}
	}
	return NULL;
}

const char * pci_device_lookup(unsigned short vendor_id, unsigned short device_id) {
	for (unsigned int i = 0; i < sizeof(_pci_devices)/sizeof(_pci_devices[0]); ++i) {
		if (_pci_devices[i].ven_id == vendor_id && _pci_devices[i].dev_id == device_id) {
			return _pci_devices[i].name;
		}
	}
	return NULL;
}

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
	char * query = NULL;

	while ((opt = getopt(argc, argv, "nq:?")) != -1) {
		switch (opt) {
			case '?':
				show_usage(argv);
				return 0;
			case 'n':
				numeric = 1;
				break;
			case 'q':
				query = optarg;
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
		if (line[0] == ' ' || line[0] == '\n') {
			/* Skip; don't care about this information */
			continue;
		}
		/* Read bus, etc. verbatim */
		char * device_bus   = line;

		/* Read device class */
		char * device_class = strstr(line," (");
		if (!device_class) {
			fprintf(stderr, "%s: parse error - expected (\n", argv[0]);
			return 1;
		}
		*device_class = '\0';
		device_class++; /* space */
		device_class++; /* ( */

		char * device_vendor = strstr(device_class, ", ");
		if (!device_vendor) {
			fprintf(stderr, "%s: parse error - expected ,\n", argv[0]);
			return 1;
		}
		*device_vendor = '\0';
		device_vendor++; /* comma */
		device_vendor++; /* space */

		char * device_code = strstr(device_vendor, ":");
		if (!device_code) {
			fprintf(stderr, "%s: parse error - expected :\n", argv[0]);
			return 1;
		}
		*device_code = '\0';
		device_code++; /* colon */

		char * last_paren = strstr(device_code, ")");
		if (!last_paren) {
			fprintf(stderr, "%s: parse error - expected )\n", argv[0]);
			return 1;
		}
		*last_paren = '\0';

		if (query) {
			unsigned short vendor_id = strtoul(device_vendor, NULL, 16);
			unsigned short device_id = strtoul(device_code,   NULL, 16);

			char * start = query;
			while (start) {
				char * colon = strchr(start, ':');
				if (!colon) return 2; /* Invalid query string */
				*colon = '\0';
				char * comma = strchr(colon+1, ',');
				if (comma) *comma = '\0';
				unsigned long query_man = strtoul(start,NULL,16);
				unsigned long query_dev = strtoul(colon+1,NULL,16);

				if (query_man == vendor_id && query_dev == device_id) return 0;

				*colon = ':';
				if (comma) {
					*comma = ',';
					start = comma + 1;
				} else {
					start = NULL;
				}
			}
		} else if (numeric) {
			fprintf(stdout, "%s %s: %s:%s\n", device_bus, device_class, device_vendor, device_code);
		} else {
			unsigned short class_id  = strtoul(device_class, NULL, 16);
			unsigned short vendor_id = strtoul(device_vendor, NULL, 16);
			unsigned short device_id = strtoul(device_code,   NULL, 16);
			const char * class_name  = pci_class_lookup(class_id);
			const char * vendor_name = pci_vendor_lookup(vendor_id);
			const char * device_name = pci_device_lookup(vendor_id, device_id);
			if (!vendor_name && !device_name) {
				fprintf(stdout, "%s %s: %s:%s\n", device_bus, class_name, device_vendor, device_code);
			} else if (!vendor_name) {
				fprintf(stdout, "%s %s: %s %s\n", device_bus, class_name, device_vendor, device_name);
			} else if (!device_name) {
				fprintf(stdout, "%s %s: %s %s\n", device_bus, class_name, vendor_name, device_code);
			} else {
				fprintf(stdout, "%s %s: %s %s\n", device_bus, class_name, vendor_name, device_name);
			}
		}
	}

	if (query) return 1;
	return 0;
}
