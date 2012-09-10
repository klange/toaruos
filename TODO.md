# TODO as of Septemember 2012

## C++
* Build with C++ support

## Terminal Fixes ##
* Investigate issues with slow terminal pipes (`yes` running amok)
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

