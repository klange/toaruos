# ToaruOS-NIH

![screenshot](https://i.imgur.com/G8G1dwf.png)

ToaruOS-NIH is a distribution of ToaruOS which contains no third-party components. Its bootloader, kernel, modules, C library, and userspace applications are all written by the ToaruOS development team and direct contributors.

This distribution aims to eventually replace the core of the mainline ToaruOS, with the various third-party components building against our own C library. This is a long-term project, and developing the C library to the point where it is useful for this purpose is not expected to be completed for quite some time.

## Pre-Built Images

Releases are occasionally posted on [GitHub](https://github.com/klange/toaru-nih/releases), and nightlies are available [from toaruos.org](https://toaruos.org/nih.iso).

### Running ToaruOS-NIH

It is recommended that you run ToaruOS-NIH in an emulator - specific Qemu or VirtualBox, though some testing has been done in VMware Workstation (reasonable, but missing driver support) and Bochs (not recommended).

#### QEMU

1GB of RAM and an Intel AC'97 sound chip are recommended:

```
qemu-system-i386 -cdrom image.iso -serial mon:stdio -m 1G -soundhw ac97,pcspk -enable-kvm -rtc base=localtime
```

You may also use OVMF with the appropriate QEMU system target:

```
qemu-system-x86_64 -cdrom image.iso -serial mon:stdio -m 1G -soundhw ac97,pcspk -enable-kvm -rtc base=localtime \
  -bios /usr/share/qemu/OVMF.fd
```

#### VirtualBox

ToaruOS should function either as an "Other/Unknown" guest or an "Other/Uknown 64-bit" guest with EFI.

All network chipset options should work except for `virtio-net` (work on virtio drivers has not yet begun).

It is highly recommended, due to the exisence of Guest Additions drivers, that you provide your VM with at least 32MB of video memory to support larger display resolutions - especially if you are using a 4K display.

Ensure that the audio controller is set to ICH AC97 and that audio output is enabled (as it is disabled by default in some versions of VirtualBox).

Keep the system chipset set to PIIX3 for best compatibility. 1GB of RAM is recommended.

#### VMWare

Support for VMWare is experimental.

As of writing, the following configuration has been tested as functioning:

- Create a virtual machine for a 64-bit guest. (ToaruOS-NIH is 32-bit, but this configuration selects some hardware defaults that are desirable)
- Ensure the VM has 1GB of RAM.
- Remove the hard disk from the VM.
- Remove the sound card from the VM. VMWare implements an Ensoniq chipset we do not have drivers for.
- Manually set the `firmware` value to `efi` in the VMX file. The BIOS loader has known issues under VMWare.
- For network settings, the NAT option is recommended.

#### Bochs

Using Bochs to run ToaruOS is not advised; however the following configuration options are recommended if you wish to try it:

- Attach the CD and set it as a boot device.
- Ensure that the `pcivga` device is enabled or ToaruOS will not be able to find the video card through PCI.
- Provide at least 512MB of RAM to the guest.
- If available, enable the `e1000` network device (this has never been tested).
- Clock settings of `sync=realtime, time0=local, rtc_sync=1` are recommended.

## Implementation Details

All source code for the entire operating system is included in this repository.

### Kernel

The NIH kernel is essentially the same as the mainline kernel, though the PCI vendor and device ID list has been replaced with our own slimmed down version. This was the only third-party element of the ToaruOS kernel. Additionally, the headers for the kernel have been relocated from their original directories to facilitate a cleaner build. The NIH kernel should be considered the latest version of the ToaruOS kernel.

### Bootloader

Mainline ToaruOS shipped with GRUB, which provided a multiboot-compatible ELF loader. To that end, our native bootloader also implements multiboot. However, as writing a feature-complete bootloader is not a goal of this project, the native bootloader is very limited, supporting only ATAPI CDs on systems with El Torito "no-emulation" support. It is not guaranteed to work on real hardware, but has been tested in QEMU, Bochs, VirtualBox, and VMware Player.

### Userspace

The userspace includes a work-in-progress C standard library, the ToaruOS native libraries, the compositor (using only in-house graphics routines), and various other first-party utilities and applications.

## Building

First, ensure you have the necessary build tools, which are mostly the same as mainline ToaruOS: `yasm`, `xorriso`, `genext2fs` (with Debian patches), `python`, `mtools` (for building FAT EFI payloads) and `gnu-efi` to build the EFI bootloader (I'll explore implementing necessary headers and functionality myself in the future, but for now just pull in gnu-efi and make my life easier).

Run `make` and you will be prompted to build a toolchain. Reply `y` and allow the toolchain to build.

## Rationale

ToaruOS's kernel is entirely in-house. Its userspace, however, is built on several third-party libraries and tools, such as the Newlib C library, Freetype, Cairo, libpng, and most notably Python. While the decision to build ToaruOS on these technologies is not at all considered a mistake, the possibility remains to build a userspace entirely from scratch.

## Goals

- **Write a basic C library.**

  To support building the native ToaruOS libraries and port some basic software, a rudimentary C library is required.

- **Remove Cairo as a dependency for the compositor.**

  Cairo is a major component of the modern ToaruOS compositor, but is the only significant third-party dependency. This makes the compositor, which is a key part of what makes ToaruOS "ToaruOS", an important inclusion in this project. Very basic work has been done to allow the compositor to build and run without Cairo, but it is a naïve approach and remains very slow. Implementing Cairo's clipping and SSE-accelerated blitting operations is a must.

- **Write a vector font library.**

  Support for TrueType/OpenType TBD, but vector fonts are critical to the visual presentation of ToaruOS.

- **Support a compressed image format.**

  ToaruOS used a lot of PNGs, but maybe writing our own format would be fun.

## Roadmap

1. Enough C to port the dynamic loader. (Done)

2. Get the VGA terminal building. (Done)

3. Get the shell running. (Done)

4. De-Cairo-tize the compositor. (Done, but more work planned)

6. Enough C to port Python. (Done, but also more work to do - some bugs exist / math stuff doesn't work)

7. Enough C to port GCC. (In progress)

## Project Layout

- **apps** - Userspace applications, all first-party.
- **base** - Ramdisk root filesystem staging directory. Includes C headers in `base/usr/include`, as well as graphical resources for the compositor and window decorator.
- **boot** - Bootloader.
- **kernel** - The ToaruOS kernel.
- **lib** - Userspace libraries.
- **libc** - C standard library implementation.
- **linker** - Userspace dynamic linker/loader, implements shared library support.
- **modules** - Kernel modules/drivers.
- **util** - Utility scripts, staging directory for the toolchain (binutils/gcc).
