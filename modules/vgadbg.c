#include <system.h>
#include <printf.h>
#include <module.h>

#include <ata.h>

static unsigned short * textmemptr = (unsigned short *)0xB8000;
static void placech(unsigned char c, int x, int y, int attr) {
	unsigned short *where;
	unsigned att = attr << 8;
	where = textmemptr + (y * 80 + x);
	*where = c | att;
}

static char vga_to_ansi[] = {
	0, 4, 2, 6, 1, 5, 3, 7,
	8,12,10,14, 9,13,11,15
};

static int fg = 0x07;
static int bg = 0x10;
static int cur_x = 0;
static int cur_y = 0;

static int write_string(char * s) {
	int written = 0;
	while (*s) {
		switch (*s) {
			case '\n':
				cur_x = 0;
				cur_y++;
				break;
			case '\b':
				if (cur_x > 0) cur_x--;
				placech(' ', cur_x, cur_y, (vga_to_ansi[fg] & 0xF) | (vga_to_ansi[bg] << 4));
				break;
			default:
				placech(*s, cur_x, cur_y, (vga_to_ansi[fg] & 0xF) | (vga_to_ansi[bg] << 4));
				cur_x++;
				break;
		}
		if (cur_x == 80) {
			cur_x = 0;
			cur_y++;
		}
		if (cur_y == 25) {
			memmove(textmemptr, (textmemptr + 80), sizeof(unsigned short) * 80 * 24);
			memset(textmemptr + 80 * 24, 0x00, 80 * sizeof(unsigned short));
			cur_y = 24;
		}
		s++;
		written++;
	}
	return written;
}

static void reset(void) {
	fg = 0x07;
	bg = 0x10;
}

static void list_files(char * directory) {
	fs_node_t * wd = kopen(directory, 0);
	uint32_t index = 0;
	struct dirent * kentry = readdir_fs(wd, index);
	while (kentry) {
		write_string(kentry->name);
		write_string("\n");
		free(kentry);

		index++;
		kentry = readdir_fs(wd, index);
	}
	close_fs(wd);
}

static void debug_ata_wait(void) {
	inportb(0x1F0 + ATA_REG_ALTSTATUS);
	inportb(0x1F0 + ATA_REG_ALTSTATUS);
	inportb(0x1F0 + ATA_REG_ALTSTATUS);
	inportb(0x1F0 + ATA_REG_ALTSTATUS);
}

static void debug_ata_primary(void) {
	/* Reset */
	char tmp[100];
	outportb(0x3F6, 0x04);

	debug_ata_wait();

	outportb(0x3F6, 0x00);

	debug_ata_wait();

	outportb(0x1F0 + ATA_REG_HDDEVSEL, 0xA0);

	debug_ata_wait();

	/* Wait on device */
	int status;
	int i = 0;
	while ((status = inportb(0x1F0 + ATA_REG_STATUS)) & ATA_SR_BSY) i++;

	sprintf(tmp, "Waited on status %d times\n", i);
	write_string(tmp);

	unsigned char cl = inportb(0x1F0 + ATA_REG_LBA1); /* CYL_LO */
	unsigned char ch = inportb(0x1F0 + ATA_REG_LBA2); /* CYL_HI */

	if (cl == 0xD0) {
		write_string("Waiting some more...\n");
		inportb(0x1F0 + ATA_REG_ALTSTATUS);
		inportb(0x1F0 + ATA_REG_ALTSTATUS);
		cl = inportb(0x1F0 + ATA_REG_LBA1); /* CYL_LO */
		ch = inportb(0x1F0 + ATA_REG_LBA2); /* CYL_HI */
	}
	sprintf(tmp, "ATA Primary 0x%2x 0x%2x\n", cl, ch);
	write_string(tmp);

	/* Now check partitions */
	mbr_t mbr;

	fs_node_t * f = kopen("/dev/hda", 0);

	if (!f) {
		write_string("Couldn't open /dev/hda\n");

	} else {

		read_fs(f, 0, 512, (uint8_t *)&mbr);

		sprintf(tmp, "signature[0] = 0x%2x\n", mbr.signature[0]);
		write_string(tmp);
		sprintf(tmp, "signature[1] = 0x%2x\n", mbr.signature[1]);
		write_string(tmp);

		write_string("Partitions:\n");

		for (int i = 0; i < 4; ++i) {
			if (mbr.partitions[i].status & 0x80) {
				sprintf(tmp, "Partition #%d: @%d+%d\n", i+1, mbr.partitions[i].lba_first_sector, mbr.partitions[i].sector_count);
				write_string(tmp);
			} else {
				sprintf(tmp, "Partition #%d: inactive\n", i+1);
				write_string(tmp);
			}
		}

	}

}

static void tasklet(void * data, char * name) {

	write_string("Tasklet created, sleeping... _");

	for (int i = 5; i > 0; i--) {
		char tmp[3];
		sprintf(tmp, "\b%d", i);
		write_string(tmp);
		unsigned long s, ss;
		relative_time(1, 0, &s, &ss);
		sleep_until((process_t *)current_process, s, ss);
		switch_task(0);
	}

	write_string("\bDone.\nReady to go.\n");

	write_string("Here's /dev:\n");
	fg = 6;
	list_files("/dev");
	reset();

	write_string("Now let's debug the primary PATA drive:\n");
	debug_ata_primary();

	reset();
	write_string("Here's /\n");
	fg = 6;
	list_files("/");
	reset();

	write_string("Here's /home");
	fg = 6;
	list_files("/home");
	reset();

}

static int vgadbg_init(void) {

	memset(textmemptr, 0x00, sizeof(unsigned short) * 80 * 25);

	write_string("VGA Text-Mode Debugger\n");
	write_string(" If you're seeing this, module loading completed successfully.\n");
	write_string(" We'll now do some checks to see what may be wrong with the system.\n");
	write_string("\n");

	create_kernel_tasklet(tasklet, "[[vgadbg]]", NULL);

	return 0;
}

static int vgadbg_fini(void) {
	return 0;
}

MODULE_DEF(vgadbg, vgadbg_init, vgadbg_fini);
