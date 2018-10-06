<p style="font-size: 16px; font-weight: bold; border: 2px solid; background-color: #f2d67b; padding: 30px; margin: 30px; text-align: center">ToaruOS-NIH has been merged. Some re-porting is still in progress, so not all functionality from ToaruOS 1.2.x is available yet.</p>

# ToaruOS

![screenshot](https://i.imgur.com/DqXLxaT.png)

ToaruOS a complete hobby operating system built from scratch. Its bootloader, kernel, modules, C library, and userspace applications and libraries are all written by the ToaruOS development team and direct contributors.

## Features

ToaruOS comes with a graphical desktop environment with a compositing window manager, a featureful terminal emulator, a shell, several command-line tools, a text editor (with syntax highlighting), a dynamic loader, and can host Python 3.6 (which is included in pre-built CD releases). The kernel and included drivers support ATA hard disks and CD drives, ext2 filesystems, ISO 9660 filesystems, PS/2 mice and keyboards, RS232 serial, Intel e1000, RTL8139, and AMD PCNet-series network chipsets, TCP/IPv4, several virtual graphical framebuffers (including Bochs/QEMU and VMware, and support for framebuffers initialized by EFI), advanced VM integration (including absolute pointing devices in VMware, QEMU, and VirtualBox, as well as automatic display resizing, "Seamless Desktop" and hardware cursors in VirtualBox specifically), an implementation of the `/proc` virtual filesystem, Unix-style pipes and TTYs, shared memory, in-memory read-write temporary filesystem, Intel AC'97 audio (with a mixer), and more.

## Pre-Built Images

Releases are occasionally posted on [GitHub](https://github.com/klange/toaru/releases), and nightlies are available [from toaruos.org](https://toaruos.org/toaru-core.iso).

### Running ToaruOS

It is recommended that you run ToaruOS in an emulator - specifically Qemu or VirtualBox, though some testing has been done in VMware Workstation (reasonable, but missing driver support) and Bochs (not recommended).

#### QEMU

1GB of RAM and an Intel AC'97 sound chip are recommended:

```
qemu-system-i386 -cdrom image.iso -serial mon:stdio -m 1G -soundhw ac97,pcspk -enable-kvm -rtc base=localtime
```

You may also use OVMF with the appropriate QEMU system target. Our EFI loader supports both IA32 and X64 EFIs:

```
qemu-system-x86_64 -cdrom image.iso -serial mon:stdio -m 1G -soundhw ac97,pcspk -enable-kvm -rtc base=localtime \
  -bios /usr/share/qemu/OVMF.fd
```

```
qemu-system-i386 -cdrom image.iso -serial mon:stdio -m 1G -soundhw ac97,pcspk -enable-kvm -rtc base=localtime \
  -bios /path/to/OVMFia32.fd
```

#### VirtualBox

ToaruOS should function either as an "Other/Unknown" guest or an "Other/Uknown 64-bit" guest with EFI.

All network chipset options should work except for `virtio-net` (work on virtio drivers has not yet begun).

It is highly recommended, due to the existence of Guest Additions drivers, that you provide your VM with at least 32MB of video memory to support larger display resolutions - especially if you are using a 4K display.

Ensure that the audio controller is set to ICH AC97 and that audio output is enabled (as it is disabled by default in some versions of VirtualBox).

Keep the system chipset set to PIIX3 for best compatibility. 1GB of RAM is recommended.

#### VMWare

Support for VMWare is experimental.

As of writing, the following configuration has been tested as functioning:

- Create a virtual machine for a 64-bit guest. (ToaruOS is 32-bit, but this configuration selects some hardware defaults that are desirable)
- Ensure the VM has 1GB of RAM.
- It is recommended you remove the hard disk and the audio device.
- For network settings, the NAT option is recommended.

#### Bochs

Using Bochs to run ToaruOS is not advised; however the following configuration options are recommended if you wish to try it:

- Attach the CD and set it as a boot device.
- Ensure that the `pcivga` device is enabled or ToaruOS will not be able to find the video card through PCI.
- Provide at least 512MB of RAM to the guest.
- If available, enable the `e1000` network device using the `slirp` backend.
- Clock settings of `sync=realtime, time0=local, rtc_sync=1` are recommended.

## Implementation Details

All source code for the entire operating system is included in this repository.

### Bootloader

ToaruOS 1.2.x and earlier shipped with GRUB, which provided a multiboot-compatible ELF loader. To that end, our native bootloader also implements multiboot. However, as writing a feature-complete bootloader is not a goal of this project, the native bootloader is very limited, supporting only ATAPI CDs on systems with El Torito "no-emulation" support. It is not guaranteed to work on real hardware, but has been tested in QEMU, Bochs, VirtualBox, and VMware Player.

### Userspace

The userspace includes a work-in-progress C standard library, the ToaruOS native libraries, the compositor (using only in-house graphics routines), and various other first-party utilities and applications.

#### Notable Components

- **Yutani**  (window compositor), [apps/compositor.c](apps/compositor.c), manages window buffers, layout, and input routing.
- **Bim** (text editor), [apps/bim.c](apps/bim.c), is a vim-inspired editor with syntax highlighting.
- **Terminal**, [apps/terminal.c](apps/terminal.c), xterm-esque terminal emulator with 256 and 24-bit color support.
- **ld.so** (dynamic linker/loader), [linker/linker.c](linker/linker.c), loads dynamically-linked ELF binaries.
- **Esh** (shell), [apps/sh.c](apps/sh.c), supports pipes, redirections, variables, and more.

## Building

First, ensure you have the necessary build tools: `yasm`, `xorriso`, `genext2fs` (with Debian patches), `python`, `mtools` (for building FAT EFI payloads) and `gnu-efi` to build the EFI bootloader (I'll explore implementing necessary headers and functionality myself in the future, but for now just pull in gnu-efi and make my life easier).

Run `make` and you will be prompted to build a toolchain. Reply `y` and allow the toolchain to build.

### Third-Party Ports

#### Python

There are instructions on building Python 3.6 available from [the gitlab wiki](https://gitlab.com/toaruos/toaruos/wikis/Installing-Python).

#### Freetype

Currently only the Terminal supports using Freetype as a text rendering backend, but this will be expanded in the future.

Freetype should mostly build as-is, though libtool needs to be taught how to build a shared object for ToaruOS called `libfreetype.so` - this is left as an exercise for the reader until I've had time to formalize the process.

Once freetype is built and installed to `base/usr`, `make ext-freetype` will build the extension library. Place the required fonts, which you can get from maineline ToaruOS, in `base/usr/share/fonts`.

With fonts available, the build scripts will build larger ramdisks to accomodate the additional files. The font server will automatically run on startup if a GUI boot target is selected, and the Terminal will automatically use the Freetype backend if it loads.

#### Cairo / Pixman

The compositor can use Cairo for rendering, which improves performance over the naïve in-house SSE-accelerated blitter.

## Backwards Compatibility Notes

No ABI or API compatibility is guaranteed through the development of ToaruOS. Until a larger corpus of third-party software is ported to our new C library, APIs may change to improve or simplify library use, or to fix bugs. Even kernel ABI compatibility is not guaranteed as system calls are improved or made more compliant with expectations of POSIX or the C standard.

## Rationale

ToaruOS's kernel is entirely in-house. Its userspace, however, is built on several third-party libraries and tools, such as the Newlib C library, Freetype, Cairo, libpng, and most notably Python. While the decision to build ToaruOS on these technologies is not at all considered a mistake, the possibility remains to build a userspace entirely from scratch.

## Goals

Many of our initial goals have been met, including sufficient C library support to port Python 3.6.

Our current unmet goals include:

- Enough C library support to port binutils/gcc (needs enough C to get libstdc++ working)
- Plugin systems for the compositor and general graphics APIs to support third-party libraries in the future (including support for Cairo as a backend for the compositor, PNG support in the graphics sprite API, Truetype rendering support through FreeType in the text rendering engine).
- Porting the complete native desktop experience from ToaruOS mainline (which mostly means porting Python prototype applications and libraries to C).

## Project Layout

- **apps** - Userspace applications, all first-party.
- **base** - Ramdisk root filesystem staging directory. Includes C headers in `base/usr/include`, as well as graphical resources for the compositor and window decorator.
- **boot** - Bootloader, including BIOS and EFI IA32 and X64 support.
- **cdrom** - Staging area for ISO9660 CD image, containing mostly blank shadow files for the FAT image.
- **ext** - Optional runtime-loaded bindings for third-party libraries.
- **fatbase** - Staging area for FAT image used by EFI.
- **kernel** - The ToaruOS kernel.
- **lib** - Userspace libraries.
- **libc** - C standard library implementation.
- **linker** - Userspace dynamic linker/loader, implements shared library support.
- **modules** - Kernel modules/drivers.
- **util** - Utility scripts, staging directory for the toolchain (binutils/gcc).
- **.make** - Generated Makefiles.

## Mirrors

ToaruOS is regularly mirrored to multiple Git hosting sites.

- Gitlab: [toaruos/toaruos](https://gitlab.com/toaruos/toaruos)
- GitHub: [klange/toaruos](https://github.com/klange/toaruos)
- Bitbucket: [klange/toaruos](https://bitbucket.org/klange/toaruos)
- ToaruOS.org: [klange/toaruos](https://git.toaruos.org/klange/toaruos)

