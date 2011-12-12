# Short-Term Development Targets as of 2011/10/31

## Harddisk Support
* Write support for EXT2
* Port EXT2 drivers to IDE -read-/write (superblock read testing was successful)
* Build a better VFS with support for disk mounting
* -Get all of the example binaries onto a hard disk image-
* **PORT GCC**

## Operation Viper
* Port ncurses
    * Requires some terminal-related C library functions
* Port Vim
    * Requires directory support
    * Disable command execution?

# Development Targets as of 2011/10/21


## I/O
* `/dev` file system
* `/dev/fbN` and `/dev/ttyN` for virtual framebuffer terminals and graphics
* `/dev/ttyS0` for serial I/O
* Support framebuffer switching via keyboard
* Remove hardcoded special-casing for `stdin`/`stdout`/`stderr`
* SATA read/write drivers (`/dev/sdaN`)
* `/dev/ramdisk` (read-only)
* EXT2 write support (including file creation, directory creation, rm, unlink, etc.)
* EXT2 drivers should operate on a `/dev/*` file
* Mounting of `/dev/*` files using a filesystem handler
* VFS tree

## libc
* Fork `newlib`
* Changes for `newlib` should be moved to a git repository
* Fix 64-bit host builds?
* `dirent.h` support; `readdir` in kernel
* Entire user-space library set should build from scratch on any compatible system
* Consider working with `glib`?

## GCC
* Port GCC
* Port `yasm`, `make`

## Task Management
* -Fix user-mode `fork`-
* -Add user-mode `wait`- (with queues!)
* Signals (`signal()`, `kill()`, etc.)


## Signals and Exceptions

SIGDIAF (Die in a fire)

# EOH
EOH, "Engineering Open House", is an annual event held at the University of Illinois at Urbana-Champaign which showcases student projects and research.

## Primary Plans
* Windowing environment
* More user applications

## Details

### Windowing Environment

EOH is focused on making stuff that looks cool. Judges are impressed by things they can see, thus the primary target for EOH is a GUI.

Complete the windowing environment (which will eventually be rebuilt to implement Wayland, in the far future), will require:

* Pipes (for user applications, terminal windows, etc.)
* Shared memory buffers (single writer; client-server model; for window graphics buffers)
* Input device files (for mouse and keyboard reads)

Additionally, it would be nice to have:

* Freetype (for smooth, unicode text rendering; I have had difficulties getting Freetype to process font files, this may be a bug in the ELF loader; try to trace what calls Freetype normally makes to figure thise out)
* There is an embedded, pure-software implementation of OpenGL; consider porting it (because 3d stuff is cool)

### Applications

Both for the purpose of demonstrating the windowing environment and to make とあるOS more usable, it would be ideal to have a few usable applications.

* A simple editor (I would love to port something real like Vim, but this has proven difficult due to external requirements)
* Graphical applications such as:
    * A calculator
    * Some simple game (perhaps minesweeper)
    * An analog clock
* A compiler for something, even if it isn't C.
