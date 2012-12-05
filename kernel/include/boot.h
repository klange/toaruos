/* vim: tabstop=4 shiftwidth=4 noexpandtab
 */
#ifndef BOOT_H
#define BOOT_H
/*
 * Boot Information Types
 * Used in the kernel boot process to determine
 * how we booted and where we can get BIOS
 * information from that bootloader.
 *
 */
#include <system.h>

/*
 * Multiboot
 * A format managed by GNU and used in GRUB.
 * Also supported natively by QEMU and a few
 * other emulators.
 */
#include <multiboot.h>

#endif /* BOOT_H */
