# TODO for 0.4.0 Distribution Release

* CD support
  * Boot from "CD"
  * Distribute with Grub
  * CD image generator in-repo
* User Interface
  * Graphical Login
  * More applications
* Stable Harddisk writes
  * Screenshot functionality
  * Attempt an installer?

# TODO as of Septemember 2012

## C++
* Build with C++ support

## Terminal Fixes ##
* Mouse features; mouse support in windowed mode
* Tab completion in shell (this is mostly a shell-specific thing)

## Windowing System ##
* Graphical Login Manager
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

* Deprecate ramdisks
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

Heh... Consider writing some of these in better languages than C. May a D servicespace?

TODO: Get D working.

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

