#pragma once

#define BASE_VERSION "ToaruOS Bootloader v2.1"
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
#define LINK_TEXT "https://toaruos.org - https://github.com/klange/toaruos"

/* Boot command line strings */
#define DEFAULT_ROOT_CMDLINE "root=/dev/ram0 root_type=tar "
#define DEFAULT_GRAPHICAL_CMDLINE "start=live-session "
#define DEFAULT_SINGLE_CMDLINE "start=terminal\037-F "
#define DEFAULT_TEXT_CMDLINE "start=--vga "
#define DEFAULT_VID_CMDLINE "vid=auto,1440,900 "
#define DEFAULT_PRESET_VID_CMDLINE "vid=preset "
#define DEFAULT_NETINIT_CMDLINE "init=/dev/ram0 "
#define NETINIT_REMOTE_URL "args=http://toaruos.org/ramdisk-1.9.3.img "
#define MIGRATE_CMDLINE "migrate "
#define DEBUG_LOG_CMDLINE "logtoserial=warning "
#define DEBUG_SERIAL_CMDLINE "kdebug "
#define DEFAULT_HEADLESS_CMDLINE "start=--headless "

/* Where to dump kernel data while loading */
#define KERNEL_LOAD_START 0x5000000

extern char * module_dir;
extern char * kernel_path;
extern char * ramdisk_path;
