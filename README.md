![Screenshot](http://i.imgur.com/mNoGjLD.png)
![More Screenshots](http://i.imgur.com/sD9QBph.png)

# ToAruOS (とあるOS) #

とあるOS (ToAruOS) is a hobby kernel and supporting userspace, built mostly from scratch, in development since December of 2010.

It was originally developed at the University of Illinois at Urbana-Champaign. For a period of time, it was the development focus of the university's [SIGOps](http://www.acm.uiuc.edu/sigops/) chapter.

This repository contains the kernel, modules, and core userspace. Some third-party libraries and utilities are required to build a working system - these are automatically retrieved by the build process.

## Kernel ##

The kernel provides a number of features one would expect from a Unix-inspired project, including process and thread support, ELF binary support, runtime-loaded modules, pipes and TTYs, a virtual file system including virtual `/proc` (inspired by Plan9 and Linux) and device files, EXT2 filesystem support, signals, and more.

## Userspace ##

The userspace provides a rich graphical environment with a composited window manager (built on Cairo), a terminal emulator (with support for xterm 256-color modes and Konsole 24-bit color modes, anti-aliased text using FreeType, and general support for some Unicode text), and other graphical demo applications.

### Third-Party Software ###

The userspace depends on a number of third-party libraries which are outside of the development scope of the project, as well as the `newlib` C library (though development of an in-house C library is planned).

Some third-party software is provided within this repository:

* `cpudet`, a `cpuid` parser.
* VL Gothic, a Japanese TrueType font.
* DejaVu, a series of popular, free TrueType fonts.
* A SHA512 hash library
* `utf8decode.h`, UTF8 decoding tools.
* A port of `glxgears`.
* Various Curses examples by Pradeep Padala.
* A list of PCI vendor and device names.
* A terminal implementation of the game "2048".

Licenses for these tools and libraries can be found [here](LICENSE.md).

The following external libraries and tools are retrieved during the build process:

* `gcc` and `binutils` - For both a cross-compiler and a native port.
* `newlib` - C library (development of an in-house C library is planned, but has not yet commenced).
* `libpng` - Used extensively by the native graphics library to provide wallpaper and icons.
* `zlib` - Dependency of `libpng`, but also generally useful.
* `freetype` - For rendering text using TrueType fonts.
* `cairo` and `pixman` - For CPU-accelerated graphics.
* `ncurses` - Terminal control library, provides `terminfo`.
* Mesa - Implementation of OpenGL (only the software rasterizer is available).
* Vim - Popular text editor.

In addition to the libraries included in the build process, others have been ported or successfully built for とある:

* Lua - Builds as-is
* MuPDF - See [klange/toaru-pdfviewer](https://github.com/klange/toaru-pdfviewer) (library must be cross compiled)
* SDL - See [klange/SDL](https://github.com/klange/SDL) (this port is outdated and not compatible with the current windowing system)
* `snes9x-sdl` - See [klange/snes9x-sdl](https://github.com/klange/snes9x-sdl)
* Bochs - Should build as-is, but may require modifications. (Depends on SDL)
* Python - A Python port has been built, but is not yet available.

### Screenshots ###

For additional screenshots, please see [the wiki](https://github.com/klange/toaruos/wiki/Screenshots).

## Testing / Building / Installation ##

Please see [Testing and Building](https://github.com/klange/toaruos/wiki/Testing-and-Building) on the wiki.

### IRC ###

For help building the kernel and userspace, join us in `#toaruos` on Freenode (`irc.freenode.net`).


