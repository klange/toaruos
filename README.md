# ToaruOS

ToaruOS is a 64-bit, hobbyist, educational, Unix-like operating system built entirely from scratch. It includes a kernel, bootloader, dynamic linker, C standard library, composited windowing system, package manager, and several utilities and applications. All components of the core operating system are original, providing a complete environment in approximately 80,000 lines of C and assembly, all of which is included in this repository.

![Screenshot](https://user-images.githubusercontent.com/223546/105782051-11f73680-5fb7-11eb-94ed-171334c3de74.png)
*Demonstration of ToaruOS's UI, terminal emulator, and text editor.*

## History

The ToaruOS project began in December 2010 and has its roots in an independent student project. The goals of the project have changed throughout its history, initially as a learning experience for the authors, and more recently as a complete, from-scratch ecosystem.

ToaruOS 1.0 was released in January, 2017, and featured a Python userspace built on Newlib. Since 1.6.x, ToaruOS has had its own C library, dependencies on third-party libraries have been removed, and most of the Python userspace has been rewritten in C. More recent releases have focused on improving the C library support, providing more ports in our package repository, and adding new features.

Work began on ToaruOS 2.0, which brings a rewritten kernel for x86-64 (and potentially other architectures) and support for SMP, in April 2021 and was merged upstream at the end of May.

## Features

- **Dynamically linked userspace** with support for runtime `dlopen`ing of additional libraries.
- **Composited graphical UI** with SSE-accelerated alpha blitting and optional Cairo backend.
- **VM integration** for absolute mouse and automatic display sizing in VirtualBox and VMware Workstation.
- **Unix-like terminal interface** including a feature-rich terminal emulator and several familiar utilities.

### Notable Components

- **Toaru Kernel**, [kernel/](kernel/), the core of the operating system. 
- **Yutani**  (window compositor), [apps/compositor.c](apps/compositor.c), manages window buffers, layout, and input routing.
- **Bim** (text editor), [apps/bim.c](apps/bim.c), is a vim-inspired editor with syntax highlighting.
- **Terminal**, [apps/terminal.c](apps/terminal.c), xterm-esque terminal emulator with 256 and 24-bit color support.
- **ld.so** (dynamic linker/loader), [linker/linker.c](linker/linker.c), loads dynamically-linked ELF binaries.
- **Esh** (shell), [apps/sh.c](apps/sh.c), supports pipes, redirections, variables, and more.
- **MSK** (package manager), [apps/msk.c](apps/msk.c), with support for online package installation.
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

To build ToaruOS from source, it is currently recommended you use a recent Debian- or Ubuntu-derived Linux host environment.

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

### Third-Party Components

Prior to ToaruOS 1.6.x, many third-party components were included by default (Python, libpng, zlib, Cairo, freetype, and so on). These are no longer part of the default distribution or build process and must be built manually. Complete guides for building these components are currently being drafted. The instructions for building Python are complete and [available from the wiki](https://github.com/klange/toaruos/wiki/How-to-Python) (note that a host installation of Python 3.6 is required to build Python 3.6 and satisfying this is left as an exercise to the reader).

Freetype and Cairo have also been successfully built under the new in-house C library. When either of these are available, optional extension bindings may be built with `make ext-freetype` and `make ext-cairo` respectively. When the Freetype extension binding is available in the OS, alongside required Truetype font files, Freetype will be used to render text in several applications (including the terminal emulator, menus, and window decorations). When the Cairo extension binding is available, it is used by the compositor to provide improved performance.

### Project Layout

- **apps** - Userspace applications, all first-party.
- **base** - Ramdisk root filesystem staging directory. Includes C headers in `base/usr/include`, as well as graphical resources for the compositor and window decorator.
- **boot** - Bootloader, including BIOS and EFI IA32 and X64 support.
- **cdrom** - Staging area for ISO9660 CD image, containing mostly blank shadow files for the FAT image.
- **ext** - Optional runtime-loaded bindings for third-party libraries.
- **fatbase** - Staging area for FAT image used by EFI.
- **kernel** - The Toaru kernel.
- **lib** - Userspace libraries.
- **libc** - C standard library implementation.
- **linker** - Userspace dynamic linker/loader, implements shared library support.
- **modules** - Kernel modules/drivers.
- **util** - Utility scripts, staging directory for the toolchain (binutils/gcc).
- **.make** - Generated Makefiles.

## Running ToaruOS

*This section is being updated to reflect changes in ToaruOS 2.0 and may be outdated or incorrect.*

### QEMU

```
qemu-system-x86_64 -M q35 -kernel misaka-kernel -initrd ramdisk.tar -append "root=/dev/ram0 start=live-session migrate" -enable-kvm -m 1G
```

Additionally, a tool is available for running QEMU, under specific environments, with automatic support for resizing the guest display resolution when the QEMU window changes size: `util/qemu-harness.py`

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

With a capable compiler toolchain, ToaruOS is able to build its own kernel, userspace, libraries, and bootloader, and turn these into a working ISO CD image.

ToaruOS is not currently capable of building most of its ports, due to a lack of a proper Posix shell and Make implementation. These are eventual goals of the project.

### Is ToaruOS a Linux distribution?

ToaruOS is a completely independent project, and all code in this repository - which is the entire codebase of the operating system, including its kernel, bootloaders, libraries, and applications - is original, written by the ToaruOS developers over the course of eight years. The complete source history, going back to when ToaruOS was nothing more than a baremetal "hello world" can be tracked through this git repository.

ToaruOS has taken inspiration from Linux in its choice of binary formats, filesystems, and its approach to kernel modules, but is not derived in any way from Linux code. ToaruOS's userspace is also influenced by the GNU utilities, but does not incorporate any GNU code.

