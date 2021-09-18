# ToaruOS

ToaruOS is a "complete" operating system for x86-64 PCs with plans for ports to other platforms.

While many independent, hobby, and research OSes aim to experiment with new designs, ToaruOS is intended as an educational resource, providing a representative microcosm of functionality found in major desktop operating systems.

The OS includes a kernel, bootloader, dynamic shared object linker, C standard library, its own composited windowing system, a dynamic bytecode-compiled programming language, advanced code editor, and dozens of other utilities and example applications.

There are no external runtime dependencies and all required source code, totalling roughly 80,000 lines of (primarily) C, is included in this repository, save for [Kuroko](https://github.com/kuroko-lang/kuroko), which lives separately.

![Screenshot](https://klange.dev/s/Screenshot%20from%202021-09-18%2013-49-35.png)
*Demonstration of ToaruOS's UI and some applications.*

## History

ToaruOS has been in development for over ten years, and the goals of the project have changed through out its life time.

When it was initiated in December 2010, the OS was a personal project, and its focus was on the individual learning of its author.

With time, ToaruOS's relatively advanced graphical interface and other features have inspired new hobby OSes, and the goals of the project shifted towards providing a reliable learning resource.

From its initial release through the middle of 2018, ToaruOS's userspace was built on top of the Newlib C standard library implementation as well as various third-party libraries such as Cairo and Freetype.

Since the release of 1.6.x, all third-pary runtime dependencies have been removed or replaced, and ToaruOS has been entirely "in-house".

In April 2021, work began on ToaruOS 2.0, which brings a rewritten kernel for x86-64 (and potentially other architectures) and support for SMP. The new "Misaka" kernel was merged upstream at the end of May.

## Features

- **Dynamically linked userspace** with support for runtime `dlopen`ing of additional libraries.
- **Composited graphical UI** with software acceleration and a late-2000s design inspiration.
- **VM integration** for absolute mouse and automatic display sizing in VirtualBox and VMware Workstation.
- **Unix-like terminal interface** including a feature-rich terminal emulator and several familiar utilities.
- **Optional third-party ports** including GCC 10.3, Binutils, SDL1.2, Quake, and more.

### Notable Components

- **Misaka** (kernel), [kernel/](kernel/), a hybrid modular kernel, and the core of the operating system.
- **Yutani** (window compositor), [apps/compositor.c](apps/compositor.c), manages window buffers, layout, and input routing.
- **Bim** (text editor), [apps/bim.c](apps/bim.c), is a vim-inspired editor with syntax highlighting.
- **Terminal**, [apps/terminal.c](apps/terminal.c), xterm-esque terminal emulator with 256 and 24-bit color support.
- **ld.so** (dynamic linker/loader), [linker/linker.c](linker/linker.c), loads dynamically-linked ELF binaries.
- **Esh** (shell), [apps/sh.c](apps/sh.c), supports pipes, redirections, variables, and more.
- **Kuroko** (interpreter), [kuroko/](https://kuroko-lang.github.io/), a dynamic bytecode-compiled programming language.

## Current Goals

The following projects are currently in progress:

- **Rewrite the network stack** for greater throughput, stability, and server support.
- **Improve SMP performance** with better scheduling and smarter userspace synchronization functions.
- **Support more hardware** with new device drivers for AHCI, USB, virtio devices, etc.
- **Bring back ports** from ToaruOS "Legacy", like muPDF and Mesa.
- **Improve POSIX coverage** especially in regards to signals, synchronization primitives, as well as by providing more common utilities.
- **Continue to improve the C library** which remains quite incomplete compared to Newlib and is a major source of issues with bringing back old ports.
- **Replace third-party development tools** to get the OS to a state where it is self-hosting with just the addition of a C compiler.
- **Implement a C compiler toolchain** in [toarucc](https://github.com/klange/toarucc).

## Building / Installation

### Building With Docker

General users hoping to build ToaruOS from source are recommended to use our prebuilt Docker image, which contains all the necessary tools:

    git clone --recursive https://github.com/klange/toaruos
    cd toaruos
    docker pull toaruos/build-tools:1.99.x
    docker run -v `pwd`:/root/misaka -w /root/misaka -e LANG=C.UTF-8 -t toaruos/build-tools:1.99.x util/build-in-docker.sh

After building like this, you can run the various utility targets (`make run`, etc.). Try `make shell` to run a ToaruOS shell using a serial port with QEMU.

### Build Process Internals

The `Makefile` uses a Kuroko tool, `auto-dep.krk`, to generate additional Makefiles for the userspace applications and libraries, automatically resolving dependencies based on `#include` directives.

In an indeterminate order, the C library, kernel, userspace librares and applications are built, combined into a compressed archive for use as a ramdisk, and then packaged into an ISO9660 filesystem image.

### Project Layout

- **apps** - Userspace applications, all first-party.
- **base** - Ramdisk root filesystem staging directory. Includes C headers in `base/usr/include`, as well as graphical resources for the compositor and window decorator.
- **boot** - BIOS and EFI loader with interactive menus.
- **build** - Auxiliary build scripts for future platform ports.
- **kernel** - The Misaka kernel.
- **kuroko** - Submodule checkout of the Kuroko interpreter.
- **lib** - Userspace libraries.
- **libc** - C standard library implementation.
- **linker** - Userspace dynamic linker/loader, implements shared library support.
- **modules** - Where loadable module sources will go when they are re-implemented for Misaka.
- **util** - Utility scripts, staging directory for the toolchain (binutils/gcc).
- **.make** - Generated Makefiles.

## Running ToaruOS

### QEMU

```
qemu-system-x86_64 -M q35 -enable-kvm -m 1G -soundhw ac97 -cdrom image.iso
```

### Other

The legacy BIOS loader has been tested in VirtualBox and VMWare. For both, set up a virtual machine with an "Other (64-bit)" guest OS and attach the CD image. A least 32MB of display memory and 1GB of RAM are recommended. Some hardware configurations may not be supported.

EFI systems are also supported, though the EFI loader remains experimental. ToaruOS can also be booted in Multiboot-compatible loaders, such as [GRUB](https://github.com/klange/toaruos-grub/tree/misaka).

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

