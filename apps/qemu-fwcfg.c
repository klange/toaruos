/*
 * Tool for reading QEMU fwcfg values.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>

unsigned short inports(unsigned short _port) {
	unsigned short rv;
	asm volatile ("inw %1, %0" : "=a" (rv) : "dN" (_port));
	return rv;
}

void outports(unsigned short _port, unsigned short _data) {
	asm volatile ("outw %1, %0" : : "dN" (_port), "a" (_data));
}

unsigned int inportl(unsigned short _port) {
	unsigned int rv;
	asm volatile ("inl %%dx, %%eax" : "=a" (rv) : "dN" (_port));
	return rv;
}

void outportl(unsigned short _port, unsigned int _data) {
	asm volatile ("outl %%eax, %%dx" : : "dN" (_port), "a" (_data));
}

unsigned char inportb(unsigned short _port) {
	unsigned char rv;
	asm volatile ("inb %1, %0" : "=a" (rv) : "dN" (_port));
	return rv;
}

void outportb(unsigned short _port, unsigned char _data) {
	asm volatile ("outb %1, %0" : : "dN" (_port), "a" (_data));
}

void outportsm(unsigned short port, unsigned char * data, unsigned long size) {
	asm volatile ("rep outsw" : "+S" (data), "+c" (size) : "d" (port));
}

void inportsm(unsigned short port, unsigned char * data, unsigned long size) {
	asm volatile ("rep insw" : "+D" (data), "+c" (size) : "d" (port) : "memory");
}

void swap_bytes(void * in, int count) {
	char * bytes = in;
	if (count == 4) {
		uint32_t * t = in;
		*t = (bytes[0] << 24) | (bytes[1] << 12) | (bytes[2] << 8) | bytes[3];
	} else if (count == 2) {
		uint16_t * t = in;
		*t = (bytes[0] << 8) | bytes[1];
	}
}

struct fw_cfg_file {
	uint32_t size;
	uint16_t select;
	uint16_t reserved;
	char name[56];
};

int usage(char * argv[]) {
	printf(
			"usage: %s [-?ln] [config name]\n"
			"\n"
			" -l     \033[3mlist available config entries\033[0m\n"
			" -n     \033[3mdon't print a new line after image\033[0m\n"
			" -?     \033[3mshow this help text\033[0m\n"
			"\n", argv[0]);
	return 1;
}

int main(int argc, char * argv[]) {

	int opt = 0;
	int list = 0;
	int no_newline = 0;

	while ((opt = getopt(argc, argv, "?ln")) != -1) {
		switch (opt) {
			case '?':
				return usage(argv);
			case 'n':
				no_newline = 1;
				break;
			case 'l':
				list = 1;
				break;
		}
	}

	if (optind >= argc && !list) {
		return usage(argv);
	}

	outports(0x510, 0x0000);
	if (inportb(0x511) != 'Q' ||
		inportb(0x511) != 'E' ||
		inportb(0x511) != 'M' ||
		inportb(0x511) != 'U') {
		fprintf(stderr, "%s: this doesn't seem to be qemu\n", argv[0]);
	}

	uint32_t count = 0;
	uint8_t * bytes = (uint8_t *)&count;
	outports(0x510,0x0019);
	for (int i = 0; i < 4; ++i) {
		bytes[i] = inportb(0x511);
	}
	swap_bytes(&count, 4);

	int found = 0;
	struct fw_cfg_file file;
	uint8_t * tmp = (uint8_t *)&file;

	for (unsigned int i = 0; i < count; ++i) {
		for (unsigned int j = 0; j < sizeof(struct fw_cfg_file); ++j) {
			tmp[j] = inportb(0x511);
		}

		swap_bytes(&file.size, 4);
		swap_bytes(&file.select, 2);

		if (list) {
			fprintf(stdout, "%s (%d byte%s)\n", file.name, (int)file.size, file.size == 1 ? "" : "s");
		} else {
			if (!strcmp(file.name, argv[optind])) {
				found = 1;
				break;
			}
		}
	}

	if (found) {
		outports(0x510, file.select);
		char * tmp = malloc(file.size);
		for (unsigned int i = 0; i < 32 && i < file.size; ++i) {
			tmp[i] = inportb(0x511);
		}
		fwrite(tmp, 1, file.size, stdout);

		if (!no_newline) {
			fprintf(stdout, "\n");
		}
	} else if (!list) {
		fprintf(stderr, "%s: config option not found\n", argv[0]);
		return 1;
	}

	return 0;
}
