#include <stdint.h>
#include <stddef.h>

#include "options.h"
#include "util.h"
#include "menu.h"
#include "text.h"
#include "multiboot.h"
#include "editor.h"

/* Basic text strings */
#define BASE_VERSION "ToaruOS Bootloader v4.0"
char * VERSION_TEXT = BASE_VERSION " (BIOS)";
char * HELP_TEXT = "<Enter> to boot, <e> to edit, or select a menu option with \030/\031/\032/\033.";
char * HELP_TEXT_OPT = "<Enter> to toggle, or select another option with \030/\031/\032/\033.";
char * COPYRIGHT_TEXT = "ToaruOS is free software under the NCSA license.";
char * LINK_TEXT = "https://toaruos.org - https://github.com/klange/toaruos";

/* Boot command line strings */
#define DEFAULT_ROOT_CMDLINE "root=/dev/ram0 "
#define DEFAULT_GRAPHICAL_CMDLINE "start=live-session "
#define DEFAULT_SINGLE_CMDLINE "start=terminal\037-F "
#define DEFAULT_TEXT_CMDLINE "start=--vga vid=text "
#define DEFAULT_VID_CMDLINE "vid=auto,1440,900 "
#define DEFAULT_PRESET_VID_CMDLINE "vid=preset "
#define MIGRATE_CMDLINE "migrate "
#define DEFAULT_HEADLESS_CMDLINE "start=--headless "

char * kernel_path = "KERNEL.";
char * ramdisk_path = "RAMDISK.IGZ";
char cmdline[1024] = {0};

/* Names of the available boot modes. */
struct bootmode boot_mode_names[] = {
	{1, "normal",   "Normal Boot"},
	{2, "vga",      "VGA Text Mode"},
	{3, "single",   "Single-User Graphical Terminal"},
	{4, "headless", "Headless"},
};

int base_sel = BASE_SEL;

extern char _bss_start[];
extern char _bss_end[];

int kmain() {
	memset(&_bss_start,0,(uintptr_t)&_bss_end-(uintptr_t)&_bss_start);

	BOOT_OPTION(_debug,       0, "Debug output",
			"Enable debug output in the bootloader and enable the",
			"serial debug log in the operating system itself.");

	BOOT_OPTION(_smp,         1, "Enable SMP",
			"SMP support may not be completely stable and can be",
			"disabled with this option if desired.");

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

	BOOT_OPTION(_qemubug,     0, "QEMU PS/2 workaround",
			"Work around a bug in QEMU's PS/2 controller",
			"prior to 6.0.50.");

	BOOT_OPTION(_migrate,     1, "Writable root",
			"Migrates the ramdisk from tarball to an in-memory",
			"temporary filesystem at boot. Needed for packages.");

	while (1) {
		/* Loop over rendering the menu */
		show_menu();

		/* Build our command line. */
		strcat(cmdline, DEFAULT_ROOT_CMDLINE);

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
			txt_debug = 1;
		}

		if (!_vbox) {
			strcat(cmdline, "novbox ");
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

		if (!_smp) {
			strcat(cmdline, "nosmp ");
		}

		if (_qemubug) {
			strcat(cmdline, "sharedps2 ");
		}

		if (!boot_edit) break;
		if (boot_editor()) break;

		boot_edit = 0;
		memset(cmdline, 0, 1024);
	}

	boot();

	while (1) {}
	return 0;
}
