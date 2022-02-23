#include <stdint.h>

struct rpitag {
	uint32_t phys_addr;
	uint32_t x;
	uint32_t y;
	uint32_t s;
	uint32_t b;
	uint32_t size;
	uint32_t ramdisk_start;
	uint32_t ramdisk_end;
};

void rpi_load_ramdisk(struct rpitag * tag, uintptr_t * ramdisk_phys_base, size_t * ramdisk_size);
void rpi_set_cmdline(char ** args_out);
