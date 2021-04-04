#include "types.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "base.h"
#include "text.h"
#include "util.h"
#include "elf.h"
#include "multiboot.h"
#include "kbd.h"
#include "options.h"

extern void bump_heap_setup(void*);
extern void krk_initVM(int);
extern unsigned long long krk_interpret(const char * src, char * fromFile);
extern void * krk_startModule(const char * name);
extern void krk_resetStack(void);
extern void krk_printResult(unsigned long long val);
extern int krk_repl(void);

uintptr_t KERNEL_LOAD_START = 0;

/* Basic text strings */
char * module_dir = "MOD";
char * kernel_path = "KERNEL.";
char * ramdisk_path = "RAMDISK.IMG";

/* Module file names - need to be ordered. */
char * modules[] = {
	"ZERO.KO",     // 0
	"RANDOM.KO",   // 1
	"SERIAL.KO",   // 2
	"DEBUG_SH.KO", // 3
	"PROCFS.KO",   // 4
	"TMPFS.KO",    // 5
	"ATA.KO",      // 6
	"EXT2.KO",     // 7
	"ISO9660.KO",  // 8
	"PS2KBD.KO",   // 9
	"PS2MOUSE.KO", // 10
	"LFBVIDEO.KO", // 11
	"VBOX.KO",     // 12
	"VMWARE.KO",   // 13
	"VIDSET.KO",   // 14
	"PACKETFS.KO", // 15
	"SND.KO",      // 16
	"AC97.KO",     // 17
	"NET.KO",      // 18
	"PCNET.KO",    // 19
	"RTL.KO",      // 20
	"E1000.KO",    // 21
	"PCSPKR.KO",   // 22
	"PORTIO.KO",   // 23
	"TARFS.KO",    // 24
	0
};

/* Names of the available boot modes. */
struct bootmode boot_mode_names[] = {
	{1, "normal",   "Normal Boot"},
	{2, "vga",      "VGA Text Mode"},
	{3, "single",   "Single-User Graphical Terminal"},
	{4, "headless", "Headless"},
};

unsigned int BASE_SEL = ((sizeof(boot_mode_names)/sizeof(*boot_mode_names))-1);

static void updateCursor(int x, int y) {
	unsigned short pos = (y * 80 + x);
	outportb(0x3D4, 0x0F);
	outportb(0x3D5, pos & 0xFF);
	outportb(0x3D4, 0x0E);
	outportb(0x3D5, (pos >> 8) & 0xFF);
}

static void backspace(void) {
	move_cursor_rel(-1,0);
	print_(" ");
	move_cursor_rel(-1,0);
}

extern char _bss_start[];
extern char _bss_end[];
int kmain() {
	memset(&_bss_start,0,(uintptr_t)&_bss_end-(uintptr_t)&_bss_start);

	BOOT_OPTION(_debug,       0, "Debug output",
			"Enable debug output in the bootloader and enable the",
			"serial debug log in the operating system itself.");

	BOOT_OPTION(_legacy_ata,  0, "Legacy ATA driver",
			"Enable the legacy ATA driver, which does not support",
			"ATAPI or use DMA. May be necessary in some virtual machines.");

	BOOT_OPTION(_normal_ata,  1, "DMA ATA driver",
			"Enable the normal, DMA-capable ATA driver. This is the default.",
			NULL);

	BOOT_OPTION(_debug_shell, 1, "Debug shell",
			"Enable the kernel debug shell. This can be accessed using",
			"the `kdebug` application.");

	BOOT_OPTION(_video,       1, "Video modules",
			"Enable the video modules. These are needed to modeset",
			"and provide a framebuffer for the UI.");

	BOOT_OPTION(_vbox,        1, "VirtualBox Guest Additions",
			"Enable integration with VirtualBox, including",
			"automatic mode setting and absolute mouse pointer.");

	BOOT_OPTION(_vboxrects,   0, "VirtualBox Seamless support",
			"(Requires Guest Additions) Enables support for the",
			"Seamless Desktop mode in VirtualBox.");

	BOOT_OPTION(_vboxpointer, 1, "VirtualBox Pointer",
			"(Requires Guest Additions) Enables support for the",
			"VirtualBox hardware pointer mapping.");

	BOOT_OPTION(_vmware,      1, "VMWare driver",
			"Enable the VMware / QEMU absolute mouse pointer,",
			"and optional guest scaling.");

	BOOT_OPTION(_vmwareres,   0, "VMware guest size",
			"(Requires VMware driver) Enables support for",
			"automatically setting display size in VMware");

	BOOT_OPTION(_sound,       1, "Audio drivers",
			"Enable the audio subsystem and AC'97 drivers.",
			NULL);

	BOOT_OPTION(_net,         1, "Network drivers",
			"Enable the IPv4 network subsystem and various",
			"network interface drivers.");

	BOOT_OPTION(_migrate,     1, "Writable root",
			"Migrates the ramdisk from tarball to an in-memory",
			"temporary filesystem at boot. Needed for packages.");

	BOOT_OPTION(_serialshell, 0, "Debug on serial",
			"Start a kernel debug shell on the first",
			"serial port.");

	BOOT_OPTION(_netinit,     0, "Netinit (QEMU local)",
			"Downloads a userspace filesystem from a local",
			"HTTP server and extracts it at boot.");

	BOOT_OPTION(_netinitr,    0, "Netinit (toaruos.org)",
			"Downloads a userspace filesystem from a remote",
			"HTTP server and extracts it at boot.");

	KERNEL_LOAD_START = 0x5000000;
	outportb(0x3D4, 0x0A);
	outportb(0x3D5, (inportb(0x3d5) & 0xc0) | 0x00);
	outportb(0x3D4, 0x0B);
	outportb(0x3D5, (inportb(0x3d5) & 0xe0) | 0x0F);
	bump_heap_setup((void*)KERNEL_LOAD_START);
	set_attr(0x07);
	clear_();
	move_cursor(0,0);
	updateCursor(0,0);


	char * data = malloc(1024);
	int read = 0;

	krk_initVM(0);
	krk_startModule("__main__");
	krk_interpret("if True:\n import kuroko\n print(f'Kuroko {kuroko.version} ({kuroko.builddate}) with {kuroko.buildenv}')", "<stdin>");
	puts("Type `license` for copyright, `exit` to return to menu.");

	while(1) {
		int inCont = 0;
		char * prompt = ">>> ";
		print_(prompt);
		data[0] = 0;
		while (1) {
			updateCursor(x,y);
			int resp = read_key();
			if (resp == '\b') {
				if (!read) continue;
				read--;
				data[read] = '\0';
				backspace();
			} else if (resp == 0xc) {
				clear_();
				print_(prompt);
				if (read) {
					char * c = &data[read-1];
					while (c > data && *c != '\n') c--;
					if (*c == '\n') c++;
					print_(c);
				}
			} else if (resp == 0x17) {
				if (read) {
					char * c = &data[read-1];
					while (c >= data && *c == ' ') {
						read--;
						data[read] = '\0';
						backspace();
						c--;
					}
					while (c >= data && *c != ' ') {
						read--;
						data[read] = '\0';
						backspace();
						c--;
					}
				}
			} else if (resp == '\n' || resp == '\r') {
				print_("\n");
				if (inCont) {
					int r = read-1;
					while (r > 0 && data[r] != '\n') {
						if (data[r] != ' ') break;
						r--;
					}
					if (data[r] == '\n') break;
				}
				if (read && (data[read-1] == ':' || inCont)) {
					prompt = "  > ";
					print_(prompt);
					data[read++] = '\n';
					data[read] = '\0';
					inCont = 1;
					continue;
				} else {
					break;
				}
			} else {
				data[read++] = resp;
				data[read] = '\0';
				char tmp[2] = {resp,0};
				print_(tmp);
			}
		}
		if (read) {
			if (!strcmp(data,"exit")) break;
			unsigned long long result = krk_interpret(data,"<stdin>");
			krk_printResult(result);
			krk_resetStack();
			data[0] = '\0';
			read = 0;
		}
	}
	scroll_disabled = 1;

	/* Loop over rendering the menu */
	show_menu();

	/* Build our command line. */
	if (_netinit || _netinitr) {
		strcat(cmdline, DEFAULT_NETINIT_CMDLINE);
		ramdisk_path = "NETINIT.";
		if (_netinitr) {
			strcat(cmdline, NETINIT_REMOTE_URL);
		}
	} else {
		strcat(cmdline, DEFAULT_ROOT_CMDLINE);
	}

	if (_migrate) {
		strcat(cmdline, MIGRATE_CMDLINE);
	}

	char * _video_command_line = DEFAULT_VID_CMDLINE;

	if (boot_mode == 1) {
		strcat(cmdline, DEFAULT_GRAPHICAL_CMDLINE);
		strcat(cmdline, _video_command_line);
	} else if (boot_mode == 2) {
		strcat(cmdline, DEFAULT_TEXT_CMDLINE);
	} else if (boot_mode == 3) {
		strcat(cmdline, DEFAULT_SINGLE_CMDLINE);
		strcat(cmdline, _video_command_line);
	} else if (boot_mode == 4) {
		strcat(cmdline, DEFAULT_HEADLESS_CMDLINE);
	}

	if (_debug) {
		strcat(cmdline, DEBUG_LOG_CMDLINE);
		txt_debug = 1;
	}

	if (_serialshell) {
		strcat(cmdline, DEBUG_SERIAL_CMDLINE);
	}

	if (_vbox && !_vboxrects) {
		strcat(cmdline, "novboxseamless ");
	}

	if (_vbox && !_vboxpointer) {
		strcat(cmdline, "novboxpointer ");
	}

	if (_vmware && !_vmwareres) {
		strcat(cmdline, "novmwareresset ");
	}

	/* Configure modules */
	if (!_normal_ata) {
		modules[6] = "NONE";
	}

	if (_legacy_ata) {
		modules[6] = "ATAOLD.KO";
	}

	if (!_debug_shell) {
		modules[3] = "NONE";
		modules[14] = "NONE";
	}

	if (!_video) {
		modules[11] = "NONE";
		modules[12] = "NONE";
		modules[13] = "NONE";
		modules[14] = "NONE";
	}

	if (!_vmware) {
		modules[13] = "NONE";
	}

	if (!_vbox) {
		modules[12] = "NONE";
	}

	if (!_sound) {
		modules[16] = "NONE";
		modules[17] = "NONE";
	}

	if (!_net) {
		modules[18] = "NONE";
		modules[19] = "NONE";
		modules[20] = "NONE";
		modules[21] = "NONE";
	}

	boot();

	while (1) {}
	return 0;
}
