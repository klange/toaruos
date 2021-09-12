# ToaruOS Bootloader

This is version 4.0 of the ToaruOS Live CD bootloader.

The bootloader targets both BIOS and EFI. The BIOS loader is limited to El Torito CD boot, while the EFI loader should work in most configurations.

The bootloader provides a menu for selecting boot options, a simple editor for further customization of the kernel command line, and a 32-bit ELF loader for loading multiboot-compatible builds of Misaka.

While much of the codebase is shared between the two platforms, the BIOS loader works very differently from the EFI loader. It includes a minimal ISO 9660 filesystem implementation for locating boot files, and also loads the entire contents of the boot medium into memory before entering protected mode and displaying the menu.

The EFI loader, meanwhile, is built for x86-64 ("x64" in MS/EFI terms), and runs as a normal EFI application in long mode to display the menu, load the kernel and ramdisk through EFI filesystem access APIs, and load it into memory. It then downgrades to protected mode to allow the kernel's multiboot entrypoint to execute (which then returns to long mode again).

While the loader implements a subset of Multiboot functionality, it is likely not suited for general use by other multiboot kernels and is tailored specifically for loading Misaka.

