#ifdef EFI_PLATFORM
#  include <efi.h>
#  include <efilib.h>
EFI_HANDLE ImageHandleIn;
#else
#  include "types.h"
#endif

#include "ata.h"
#include "text.h"
#include "util.h"
#ifndef EFI_PLATFORM
#include "atapi_imp.h"
#include "iso9660.h"
#endif
#include "elf.h"
#include "multiboot.h"
#include "kbd.h"
#include "options.h"

/* Basic text strings */
#define BASE_VERSION "ToaruOS-NIH Bootloader v2.0"
#ifdef EFI_PLATFORM
#  if defined(__x86_64__)
#    define VERSION_TEXT BASE_VERSION " (EFI, X64)"
#  else
#    define VERSION_TEXT BASE_VERSION " (EFI, IA32)"
#  endif
#else
#  define VERSION_TEXT BASE_VERSION " (BIOS)"
#endif
#define HELP_TEXT "Press <Enter> or select a menu option with \030/\031/\032/\033."
#define COPYRIGHT_TEXT "ToaruOS is free software under the NCSA license."
#define LINK_TEXT "https://toaruos.org - https://gitlab.com/toaruos"

/* Boot command line strings */
#define DEFAULT_ROOT_CMDLINE "root=/dev/ram0,nocache "
#define DEFAULT_GRAPHICAL_CMDLINE "start=live-session "
#define DEFAULT_SINGLE_CMDLINE "start=terminal\037-F "
#define DEFAULT_TEXT_CMDLINE "start=--vga "
#define DEFAULT_VID_CMDLINE "vid=auto,1440,900 "
#define DEFAULT_PRESET_VID_CMDLINE "vid=preset "
#define DEFAULT_NETINIT_CMDLINE "init=/dev/ram0 "
#define MIGRATE_CMDLINE "migrate "
#define DEBUG_LOG_CMDLINE "logtoserial=warning "
#define DEBUG_SERIAL_CMDLINE "kdebug "
#define DEFAULT_HEADLESS_CMDLINE "start=--headless "

char * module_dir = "MOD";
char * kernel_path = "KERNEL.";
char * ramdisk_path = "RAMDISK.IMG";

#ifdef EFI_PLATFORM
int _efi_do_mode_set = 0;
#endif

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
	0
};

/* Names of the available boot modes. */
static struct bootmode boot_mode_names[] = {
	{1, "normal",   "Normal Boot"},
#ifndef EFI_PLATFORM
	{2, "vga",      "VGA Text Mode"},
#endif
	{3, "single",   "Single-User Graphical Terminal"},
	{4, "headless", "Headless"},
};

/* More bootloader implementation that depends on the module config */
#include "moremultiboot.h"

#ifdef EFI_PLATFORM
EFI_STATUS
	EFIAPI
efi_main (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
	InitializeLib(ImageHandle, SystemTable);
	ST = SystemTable;
	ImageHandleIn = ImageHandle;
#else
int kmain() {
#endif

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

	BOOT_OPTION(_vmware,      1, "VMWare mouse driver",
			"Enable the VMware / QEMU absolute mouse pointer.",
			NULL);

	BOOT_OPTION(_sound,       1, "Audio drivers",
			"Enable the audio subsystem and AC'97 drivers.",
			NULL);

	BOOT_OPTION(_net,         1, "Network drivers",
			"Enable the IPv4 network subsystem and various",
			"network interface drivers.");

	BOOT_OPTION(_migrate,     1, "Writable root",
			"Migrates the ramdisk from ext2 to an in-memory",
			"temporary filesystem at boot.");

	BOOT_OPTION(_serialshell, 0, "Debug on serial",
			"Start a kernel debug shell on the first",
			"serial port.");

	BOOT_OPTION(_netinit,     0, "Netinit",
			"Downloads a userspace filesystem from a remote",
			"server and extracts it at boot.");

	BOOT_OPTION(_vboxrects,   0, "VirtualBox Seamless support",
			"(Requires Guest Additions) Enables support for the",
			"Seamless Desktop mode in VirtualBox.");

	BOOT_OPTION(_vboxpointer, 1, "VirtualBox Pointer",
			"(Requires Guest Additions) Enables support for the",
			"VirtualBox hardware pointer mapping.");

#ifdef EFI_PLATFORM
	BOOT_OPTION(_efilargest,  0, "Prefer largest mode.",
			"When using EFI mode setting, use the largest mode.",
			NULL);

	BOOT_OPTION(_efi1024,     0, "Prefer 1024x768",
			"If a 1024x768x32 mode is found, set that.",
			NULL);

	BOOT_OPTION(_efi1080p,    0, "Prefer 1080p",
			"If a 1920x1080 mode is found, set that.",
			NULL);

	BOOT_OPTION(_efiask,      0, "Ask for input on each mode.",
			"Displays a y/n prompt for each possible mode.",
			NULL);
#endif

	/* Loop over rendering the menu */
	show_menu();

	/* Build our command line. */
	if (_netinit) {
		strcat(cmdline, DEFAULT_NETINIT_CMDLINE);
		ramdisk_path = "NETINIT.";
	} else {
		strcat(cmdline, DEFAULT_ROOT_CMDLINE);

		if (_migrate) {
			strcat(cmdline, MIGRATE_CMDLINE);
		}
	}

	char * _video_command_line = DEFAULT_VID_CMDLINE;
#ifdef EFI_PLATFORM
	_efi_do_mode_set = (_efilargest ? 1 : (_efi1024 ? 2 : (_efi1080p ? 3 : (_efiask ? 4 : 0))));
	if (_efi_do_mode_set) {
		_video_command_line = DEFAULT_PRESET_VID_CMDLINE;
	}
#endif

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
