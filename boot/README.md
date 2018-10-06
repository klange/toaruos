# ToaruOS-NIH Bootloader

This is a simple, limited BIOS and EFI bootloader for ToaruOS kernels.

It provides an implementation of the Multiboot bootloader standard.

The BIOS loader includes an ISO 9660 filesystem driver, and is suitable for use as an El Torito no-emulation boot image for use on a CD.

The EFI loader is built as both a 64-bit and 32-bit EFI executable and uses the EFI interfaces for file system access, and as such is suitable for a wider variety of environments.

Both loaders are based on the same codebase and implement the same menu system, which can be found `cstuff.c`.

The EFI loader is built using GNU-EFI, but does not use any of its convenience library functions.

