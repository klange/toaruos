# ToaruOS

ToaruOS is a 64-bit, hobbyist, educational, Unix-like operating system built entirely from scratch. It includes a kernel, bootloader, dynamic linker, C standard library, composited windowing system, and several utilities and applications. All components of the core operating system are original, providing a complete environment in approximately 80,000 lines of C and assembly, all of which is included in this repository.

![Screenshot](https://user-images.githubusercontent.com/223546/105782051-11f73680-5fb7-11eb-94ed-171334c3de74.png)
*Demonstration of ToaruOS's UI, terminal emulator, and text editor.*

## History

The ToaruOS project began in December 2010 and has its roots in an independent student project. The goals of the project have changed throughout its history, initially as a learning experience for the authors, and more recently as a complete, from-scratch ecosystem.

ToaruOS 1.0 was released in January, 2017, and featured a Python userspace built on Newlib. Since 1.6.x, ToaruOS has had its own C library, dependencies on third-party libraries have been removed, and most of the Python userspace has been rewritten in C. More recent releases have focused on improving the C library support, providing more ports in our package repository, and adding new features.

In April, 2021, work began on ToaruOS 2.0, which brings a rewritten kernel for x86-64 (and potentially other architectures) and support for SMP. The new "Misaka" kernel was merged upstream at the end of May. Completion of ToaruOS 2.0 is currently ongoing.

## Features

- **Dynamically linked userspace** with support for runtime `dlopen`ing of additional libraries.
- **Composited graphical UI** with SSE-accelerated alpha blitting and optional Cairo backend.
- **VM integration** for absolute mouse and automatic display sizing in VirtualBox and VMware Workstation.
- **Unix-like terminal interface** including a feature-rich terminal emulator and several familiar utilities.

### Notable Components

- **Misaka Kernel**, [kernel/](kernel/), the core of the operating system.
- **Yutani**  (window compositor), [apps/compositor.c](apps/compositor.c), manages window buffers, layout, and input routing.
- **Bim** (text editor), [apps/bim.c](apps/bim.c), is a vim-inspired editor with syntax highlighting.
- **Terminal**, [apps/terminal.c](apps/terminal.c), xterm-esque terminal emulator with 256 and 24-bit color support.
- **ld.so** (dynamic linker/loader), [linker/linker.c](linker/linker.c), loads dynamically-linked ELF binaries.
- **Esh** (shell), [apps/sh.c](apps/sh.c), supports pipes, redirections, variables, and more.
- **Kuroko**, [kuroko/](https://kuroko-lang.github.io/), a dynamic bytecode-compiled programming language.

## Current Goals

The following projects are currently in progress:

- **Stabilize SMP support** by cleaning up locks and other synchronization issues in the kernel.
- **Support more hardware** with new device drivers for AHCI, USB, virtio devices, etc.
- **Improve POSIX coverage** especially in regards to signals, synchronization primitives, as well as by providing more common utilities.
- **Continue to improve the C library** which remains quite incomplete compared to Newlib and is a major source of issues with bringing back old ports.
- **Replace third-party development tools** to get the OS to a state where it is self-hosting with just the addition of a C compiler.
- **Implement a C compiler toolchain** in [toarucc](https://github.com/klange/toarucc).

## Building / Installation

*This section is being updated to reflect changes in ToaruOS 2.0 and may be outdated or incorrect.*

To build ToaruOS from source, it is currently recommended you use a recent Debian- or Ubuntu-derived Linux host environment. Our build machines generally run Ubuntu 20.04 (the current LTS as of writing).

Several packages are necessary for the build system: `build-essential` (to build the cross-compiler), `xorriso` (to create CD images), `python3` (various build scripts), `mtools` (for building FAT EFI system partitions), `gnu-efi` (for building the EFI loaders).

Beyond package installation, no part of the build needs root privileges.

The build process has two parts: building a cross-compiler, and building the operating system. The cross-compiler uses GCC 10.3 and can be built by pulling the submodules `util/binutils-gdb` and `util/gcc` and running `util/build-toolchain.sh`. Generally, this only needs to be done once, and the cross-compiler does not depend on any of the components built for the operating system itself, though some components may have soft dependencies on the libc. Once the cross-compiler has been built, `make` will continue to build the operating system itself.

### Building With Docker

You can skip the process of building the cross-compiler toolchain (which doesn't get updated very often anyway) by using our pre-built toolchain through Docker:

    git clone --recursive https://github.com/klange/toaruos
    cd toaruos
    docker pull toaruos/build-tools:1.99.x
    docker run -v `pwd`:/root/misaka -w /root/misaka -e LANG=C.UTF-8 -t toaruos/build-tools:1.8.x util/build-in-docker.sh

After building like this, you can run the various utility targets (`make run`, etc.). Try `make shell` to run a ToaruOS shell (using QEMU and a network socket - you'll need netcat for this to work).

### Build Process Internals

The `Makefile` uses a Kuroko tool, `auto-dep.krk`, to generate additional Makefiles for the userspace applications libraries, automatically resolving dependencies based on `#include` directives.

In an indeterminate order, the C library, kernel, userspace librares and applications are built.

### Project Layout

- **apps** - Userspace applications, all first-party.
- **base** - Ramdisk root filesystem staging directory. Includes C headers in `base/usr/include`, as well as graphical resources for the compositor and window decorator.
- **boot** - Old bootloaders, not yet ready for ToaruOS 2.0.
- **kernel** - The Misaka kernel.
- **lib** - Userspace libraries.
- **libc** - C standard library implementation.
- **linker** - Userspace dynamic linker/loader, implements shared library support.
- **modules** - Where loadable module sources will go when they are re-implemented for Misaka.
- **util** - Utility scripts, staging directory for the toolchain (binutils/gcc).
- **.make** - Generated Makefiles.

## Running ToaruOS

Currently, the build tools in this repository will produce a kernel binary and a compressed ramdisk, which can be used with a Multiboot-compliant loader such as GRUB. QEMU also provides direct support for loading Multiboot kernels:

### QEMU

```
qemu-system-x86_64 -M q35 -kernel misaka-kernel -initrd ramdisk.igz -append "root=/dev/ram0 start=live-session migrate" -enable-kvm -m 1G
```

### Other

Until the native bootloaders are ready for ToaruOS 2.0, testing in other virtual machines can be done [with GRUB](https://github.com/klange/toaruos-grub/tree/misaka).

## Community

### Mirrors

ToaruOS is regularly mirrored to multiple Git hosting sites.

- Gitlab: [toaruos/toaruos](https://gitlab.com/toaruos/toaruos)
- GitHub: [klange/toaruos](https://github.com/klange/toaruos)
- Bitbucket: [klange/toaruos](https://bitbucket.org/klange/toaruos)
- ToaruOS.org: [klange/toaruos](https://git.toaruos.org/klange/toaruos)

### IRC

`#toaruos` on Libera (`irc.libera.chat`)

## FAQs

### Is ToaruOS self-hosting?

Currently, in the development of ToaruOS 2.0, self-hosting builds have not been tested and some utilities may be missing.

Previously, with a capable compiler toolchain, ToaruOS 1.x was able to build its own kernel, userspace, libraries, and bootloader, and turn these into a working ISO CD image.

ToaruOS is not currently capable of building most of its ports, due to a lack of a proper POSIX shell and Make implementation. These are eventual goals of the project.

### Is ToaruOS a Linux distribution?

ToaruOS is a completely independent project, and all code in this repository - which is the entire codebase of the operating system, including its kernel, bootloaders, libraries, and applications - is original, written by the ToaruOS developers over the course of eight years. The complete source history, going back to when ToaruOS was nothing more than a baremetal "hello world" can be tracked through this git repository.

ToaruOS has taken inspiration from Linux in its choice of binary formats, filesystems, and its approach to kernel modules, but is not derived in any way from Linux code. ToaruOS's userspace is also influenced by the GNU utilities, but does not incorporate any GNU code.

