# Compositor functionality
* Alt+Tab keyboard window switching (with previews)
* Advertise window titles + icons
* New panel with window list
* Rewrite client-server protocol
  * New resize handshake
  * Only sane event path; fix window creation (fix "wait for reply" commands)
  * Better packet structs for events (window.left? ew!)

# Japanese / i18n updates
* IME
* Support for multibyte/wide characters in rline and terminal linebuffer
* JIS keyboard mapping in the compositor (config file???)
  * Applications shouldn't care.
  * What to do with with special characters?

# Multiarch / Ports

* Split out x86 / PC-specific bits into new arch/ directories
* Fix up uses of explicitly sized types where possible
* x86-64 port
  * Good for verification of multi-arch readiness
  * Much easier to develop for than ARM port
* ARMv6 port is underway

# Cleanup and POSIX expansion

* Rewrite underlying process model
  * `task`/`process` split should match architecture dependency split
  * Better allocation management, process tracking
  * Cleanup is a pain right now - we need to stop eating processes that finish with parents that might want their statuses.
  * Sleeping, wait queues, and IO polling/select depend on some rewriting

# New EXT2 drivers

* The EXT2 drivers are poorly written, they need a complete rewrite
* Allow for multiple EXT2 instances
* Operate on block devices rather than directly with ATA drives
  * Allows for effortless ramdisk implementation with same code
  * Will make MMC port a lot easier for RPi

# TODO as of November, 2012

* force a build run

* ~~Integrate Cairo into build toolchain~~
  * autogen means extra effort needed
  * required to ship with new compositor
  * this has been done for a while
* ~~Fix static initializers in C++~~
  * The best method for this is probably going to be writing a dynamic loader, so...
  * this has been done for a while
* Write a dynamic loader
  * as of March 2014, finally starting to work on this
* Pretty much everything below

# TODO for 0.4.0 Distribution Release

* User Interface
  * ~~Graphical Login~~
  * More applications
* ~~Working boot on real hardware~~
  * Confirmed working on Mini 9 with hard disk boot

# TODO for 0.5.0

* CD support
  * Boot from "CD"
  * Distribute with Grub
  * CD image generator in-repo
* Stable Harddisk writes
  * Screenshot functionality
  * Attempt an installer?
  * ATA writes in general seem to be stable, work on EXT2 FS write support

# Doc Revamp

* ~~Get rid of old, outdated TeX/PDF manual~~
* Build a modern doxygen-powered documentation system for
  kernel functions for use by kernel developers.
  * Tried to do this, doxygen got very angry - might look at it again later?
* Also include doxygen documentation for included libraries
  (lib/graphics, etc.)

# TODO for Microkernel Launch (0.5.0?)

* ~~Replace ramdisks with ELF service executables~~
  * Boot with multiple modules = boot with multiple services.
  * vfs.srv, for example
  * replace "services" with "modules" and this is done...
* VFS as a service.
  * It would be super awesome to write this in a language that is more flexible.
  * Actual file system drivers as separate modules, or what?
* Service bindings
  * Essentially, a system call interface to discovering available services.
  * `require_service(...)` system call for usable errors when a service is missing?
    * Do module dependencies count? That's in...
* ~~Deprecate old graphics applications~~
  * ~~And rename the windowed versions.~~
* ~~Environment variables~~
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
  * Keeping as core for now, possibly a module in the new revision? VFS is kinda important...
* `ext2.srv` Ext2 file system server. (provides `/`)
  * module
* `ata.srv` ATA disk access server. (provides `/dev/hd*`)
  * module
* `compositor.srv` The window compositing server. (provides shmem regions)
  * staying in userspace
* `ps2_hid.srv` The keyboard/mouse server. (provides `/dev/input/ps2/*`)
  * two modules: `ps2kbd` and `ps2mouse`, and they provide `/dev/kbd` and `/dev/mouse` respectively.
  * I'll look into the other dev nodes later...
* `serial.srv` UART serial communication server (provides `/dev/ttyS*`)
  * module: `serial`

### Future Servers

* `usb.srv` Generic USB device server (provides `/dev/input/usb/*`)
* `proc.srv` Process information server (provides `/dev/proc`; uses lots of kernel bindings)
* `net.srv` Networking server (provides `/dev/net`)
* `gfx.srv` Block-access graphics server (provides `/dev/fb*`)
  * This is a module at the moment; might be abandoning a lot of these service ideas...

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
* ~~VFS support is still almost entirely non-existent~~
  * VFS works pretty nicely.
* Write support for EXT2 is still sketchy
  * This is still true as of March 2014 - actually, it's worse, write support is temporarily gone
* Still lacking fast read/write for IDE - needs more DMA!
  * Still true in 2014

## Toolchain
* ~~Finish GCC port~~
  * Finished in April/May, 2013
* ~~Port ncurses/vim/etc.~~
  * This was compleated circa March 2013
  * Native development requires good tools.
  * Also port genext2fs.
* ~~Directory support needs to be better integrated into the C library still~~
  * `readdir` and family have been integrated

## Microkernal Readiness

* ~~Deprecate ramdisks~~ **replaced with new ramdisk module, works better**
  * Haven't used them in development in over a year
  * Not useful anywhere else due to their limiting sizes
  * As of March 2014, ramdisks have been reimplemented (again) in an actual ramdisk *module*
* ~~implement module execution~~
  * Instead of loading a ramdisk, modules should be standard binaries
    * Actually, both are supported now, so that's done.
  * The binaries will be executed in a new "service mode"
    * This was isn't going to happen, at least not for kernel modules.
* Implement "servicespace"
  * Userspace, but at a different ring
  * Special access features, like extended port access
  * Higher priority scheduling

### Services to Implement

* PCI Service
* Graphics Management Service
* Compositor as a service?
  * Technically, it already is.
* Virtual File System Service

## Old I/O goals

### I/O
* ~~`/dev` file system~~
* ~~`/dev/fbN` and `/dev/ttyN` for virtual framebuffer terminals and graphics~~
  * Framebuffers still need work to actually support mapping, but can provide size/address information via `ioctl`.
* ~~`/dev/ttyS0` for serial I/O~~
* SATA read/write drivers (`/dev/sdaN`)
* ~~`/dev/ramdisk` (read-only)~~
  * Actually in `/dev/ramN` now
* ~~EXT2 drivers should operate on a `/dev/*` file~~
  * ~~Working on this with new ext2 drivers~~
    * The new driver is in the current strawberry head
  * ~~Need better block device handling; \*64() operations~~
    * Handled in the block device by doing extra reads, read-before-write operations
* ~~Mounting of `/dev/*` files using a filesystem handler~~
  * A workaround is provided for `/dev` so that mounted nodes can be listed
  * A general solution is being worked on in the new VFS
* ~~VFS tree~~

