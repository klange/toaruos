# Development Targets as of 2011/10/21


## I/O
* `/dev` file system
* `/dev/fbN` and `/dev/ttyN` for virtual framebuffer terminals and graphics
* Support framebuffer switching via keyboard
* Remove hardcoded special-casing for `stdin`/`stdout`/`stderr`
* SATA read/write drivers (`/dev/sdaN`)
* `/dev/ramdisk` (read-only)
* EXT2 write support (including file creation, directory creation, rm, unlink, etc.)
* EXT2 drivers should operate on a `/dev/*` file
* Mounting of `/dev/*` files using a filesystem handler
* VFS tree

## `libc`
* Fork `newlib`
* Changes for `newlib` should be moved to a git repository
* Fix 64-bit host builds?
* `dirent.h` support; `readdir` in kernel
* Entire user-space library set should build from scratch on any compatible system

## GCC
* Port GCC
* Port `yasm`, `make`

## Task Management
* Fix user-mode `fork`
* Add user-mode `wait` (with queues!)
* Signals (`signal()`, `kill()`, etc.)
