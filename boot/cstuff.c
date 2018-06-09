//#define __DEBUG__

#include "types.h"
#include "ata.h"
#include "text.h"
#include "util.h"
#include "atapi_imp.h"
#include "iso9660.h"
#include "elf.h"
#include "multiboot.h"
#include "kbd.h"
#include "options.h"

/* Basic text strings */
#define VERSION_TEXT "ToaruOS-NIH Bootloader v1.2"
#define HELP_TEXT "Press <Enter> or select a menu option with \030/\031."
#define COPYRIGHT_TEXT "ToaruOS is free software under the NCSA license."
#define LINK_TEXT "https://toaruos.org - https://gitlab.com/toaruos"

/* Boot command line strings */
#define DEFAULT_ROOT_CMDLINE "root=/dev/ram0,nocache "
#define DEFAULT_GRAPHICAL_CMDLINE "start=live-session "
#define DEFAULT_SINGLE_CMDLINE "start=terminal "
#define DEFAULT_TEXT_CMDLINE "start=--vga "
#define DEFAULT_VID_CMDLINE "vid=auto,1440,900 "
#define MIGRATE_CMDLINE "start=--migrate _"
#define DEBUG_LOG_CMDLINE "logtoserial=3 "

#define KERNEL_PATH "KERNEL."
#define RAMDISK_PATH "RAMDISK.IMG"
#define MODULE_DIRECTORY "MOD"

/* Where to dump kernel data while loading */
#define KERNEL_LOAD_START 0x300000

/* Module file names - need to be ordered. */
static char * modules[] = {
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
	"VBOXGUES.KO", // 12
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
	0
};

/* Names of the available boot modes. */
static char * boot_mode_names[] = {
	"Normal Boot",
	"VGA Text Mode",
	"Single-User Graphical Terminal",
};

/* More bootloader implementation that depends on the module config */
#include "moremultiboot.h"

int kmain() {

	/* Boot options - configurable values */

	BOOT_OPTION(_debug,       0, "Enable debug output.",
		"Enable debug output in the bootloader and enable the",
		"serial debug log in the operating system itself.");

	BOOT_OPTION(_legacy_ata,  0, "Enable legacy ATA driver.",
		"Enable the legacy ATA driver, which does not support",
		"ATAPI or use DMA. May be necessary in some virtual machines.");

	BOOT_OPTION(_normal_ata,  1, "Enable DMA ATA driver.",
		"Enable the normal, DMA-capable ATA driver. This is the default.",
		NULL);

	BOOT_OPTION(_debug_shell, 1, "Enable debug shell.",
		"Enable the kernel debug shell. This can be accessed using",
		"the `kdebug` application.");

	BOOT_OPTION(_video,       1, "Enable video modules.",
		"Enable the video modules. These are needed to modeset",
		"and provide a framebuffer for the UI.");

	BOOT_OPTION(_vbox,        1, "Enable VirtualBox Guest Additions.",
		"Enable integration with VirtualBox, including",
		"automatic mode setting and absolute mouse pointer.");

	BOOT_OPTION(_vmware,      1, "Enable VMWare mouse driver.",
		"Enable the VMware / QEMU absolute mouse pointer.",
		NULL);

	BOOT_OPTION(_sound,       1, "Enable audio drivers.",
		"Enable the audio subsystem and AC'97 drivers.",
		NULL);

	BOOT_OPTION(_net,         1, "Enable network drivers.",
		"Enable the IPv4 network subsystem and various",
		"network interface drivers.");

	BOOT_OPTION(_migrate,     1, "Enable writable root.",
		"Migrates the ramdisk from ext2 to an in-memory",
		"temporary filesystem at boot.");

	/* Loop over rendering the menu */
	show_menu();

	/* Build our command line. */
	strcat(cmdline, DEFAULT_ROOT_CMDLINE);

	if (_migrate) {
		strcat(cmdline, MIGRATE_CMDLINE);
	}

	if (boot_mode == 0) {
		strcat(cmdline, DEFAULT_GRAPHICAL_CMDLINE);
		strcat(cmdline, DEFAULT_VID_CMDLINE);
	} else if (boot_mode == 1) {
		strcat(cmdline, DEFAULT_TEXT_CMDLINE);
	} else if (boot_mode == 2) {
		strcat(cmdline, DEFAULT_SINGLE_CMDLINE);
		strcat(cmdline, DEFAULT_VID_CMDLINE);
	}

	if (_debug) {
		strcat(cmdline, DEBUG_LOG_CMDLINE);
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
}
