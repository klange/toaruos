# TODO as of November, 2012

* force a build run

* Integrate Cairo into build toolchain
  * autogen means extra effort needed
  * required to ship with new compositor
* Fix static initializers in C++
  * The best method for this is probably going to be writing a dynamic loader, so...
* Write a dynamic loader
* Pretty much everything below

# TODO for 0.4.0 Distribution Release

* CD support
  * Boot from "CD"
  * Distribute with Grub
  * CD image generator in-repo
* User Interface
  * ~~Graphical Login~~
  * More applications
* Stable Harddisk writes
  * Screenshot functionality
  * Attempt an installer?

# Doc Revamp

* Get rid of old, outdated TeX/PDF manual
* Build a modern doxygen-powered documentation system for
  kernel functions for use by kernel developers.
* Also include doxygen documentation for included libraries
  (lib/graphics, etc.)

# TODO for Microkernel Launch (0.5.0?)

* Replace ramdisks with ELF service executables
  * Boot with multiple modules = boot with multiple services.
  * vfs.srv, for example
* VFS as a service.
  * It would be super awesome to write this in a language that is more flexible.
  * Actual file system drivers as separate modules, or what?
* Service bindings
  * Essentially, a system call interface to discovering available services.
  * `require_service(...)` system call for usable errors when a service is missing?
* Deprecate old graphics applications
  * And rename the windowed versions.
* Environment variables
  * Support them in general
  * Push things like graphics parameters to environment variables
* Integrate service-based VFS into C library
  * Which probably means integrating shmem services into the C library
* Services in a separate ring
  * Compositor as a service
  * Compositor shmem names integrated with service discovery
* For VFS, need better IPC for cross-process read/write/info/readdir/etc. calls

## Service Modules (aka "Services")

* `vfs.srv` The virtual file system server. (required to provide file system endpoints)
* `ext2.srv` Ext2 file system server. (provides `/`)
* `ata.srv` ATA disk access server. (provides `/dev/hd*`)
* `compositor.srv` The window compositing server. (provides shmem regions)
* `ps2_hid.srv` The keyboard/mouse server. (provides `/dev/input/ps2/*`)
* `serial.srv` UART serial communication server (provides `/dev/ttyS*`)

### Future Servers

* `usb.srv` Generic USB device server (provides `/dev/input/usb/*`)
* `proc.srv` Process information server (provides `/dev/proc`; uses lots of kernel bindings)
* `net.srv` Networking server (provides `/dev/net`)
* `gfx.srv` Block-access graphics server (provides `/dev/fb*`)

## Things that are not services

* ELF support is not a service

# TODO as of Septemember 2012

## C++
* ~~Build with C++ support~~

## Terminal Fixes ##
* Mouse features; mouse support in windowed mode
* ~~Tab completion in shell (this is mostly a shell-specific thing)~~

## Windowing System ##
* ~~Graphical Login Manager~~
* Finish GUI toolkit
* File manager app

## Harddisk Drive Extras
* VFS support is still almost entirely non-existent
* Write support for EXT2 is still sketchy
* Still lacking fast read/write for IDE - needs more DMA!

## Toolchain
* Finish GCC port
  * Still missing a few things in the underlying C library
  * Ideally, also want to be able to build natively, so need scripting, build utils, etc.
* Port ncurses/vim/etc.
  * Native development requires good tools.
  * Also port genext2fs.
* Directory support needs to be better integrated into the C library still

## Microkernal Readiness

* ~~Deprecate ramdisks~~ **replaced with new ramdisk module, works better**
  * Haven't used them in development in over a year
  * Not useful anywhere else due to their limiting sizes
* Implement module execution
  * Instead of loading a ramdisk, modules should be standard binaries
  * The binaries will be executed in a new "service mode"
* Implement "servicespace"
  * Userspace, but at a different ring
  * Special access features, like extended port access
  * Higher priority scheduling

### Services to Implement

* PCI Service
* Graphics Management Service
* Compositor as a service?
* Virtual File System Service

## Old I/O goals

### I/O
* `/dev` file system
* `/dev/fbN` and `/dev/ttyN` for virtual framebuffer terminals and graphics
* `/dev/ttyS0` for serial I/O
* SATA read/write drivers (`/dev/sdaN`)
* `/dev/ramdisk` (read-only)
* EXT2 drivers should operate on a `/dev/*` file
* Mounting of `/dev/*` files using a filesystem handler
* VFS tree

