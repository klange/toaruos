![Screenshot](http://i.imgur.com/z1aFy0C.png)
![More Screenshots](http://i.imgur.com/g3zclG2.png)

# ToAruOS (とあるOS) #

とあるOS (ToAruOS) is a hobby kernel and supporting userspace, built mostly from scratch, in development since December of 2010.

It was originally developed at the University of Illinois at Urbana-Champaign. For a period of time, it was the development focus of the university's [SIGOps](http://www.acm.uiuc.edu/sigops/) chapter.

This repository contains the kernel, modules, and core userspace. Some third-party libraries and utilities are required to build a working system - these are automatically retrieved by the build process.

## IRC ##

For discussion, help with building or running the OS, and for up-to-date build verification, please join us in `#toaruos` on Freenode (`irc.freenode.net`).

## Features ##

The kernel provides a number of features one would expect from a Unix-inspired project, including process and thread support, ELF binary support, runtime-loaded modules, pipes and TTYs, a virtual file system including virtual `/proc` (inspired by Plan9 and Linux) and device files, EXT2 filesystem support, signals, and more.

The userspace provides a rich graphical environment with a composited window manager (built on Cairo), a terminal emulator (with support for xterm 256-color modes and Konsole 24-bit color modes, anti-aliased text using FreeType, and general support for some Unicode text), and other graphical demo applications.

### Third-Party Software ###

The userspace depends on a number of third-party libraries which are outside of the development scope of the project, as well as the `newlib` C library (though development of an in-house C library is planned).

Some third-party software is provided within this repository:

* `cpudet`, a `cpuid` parser.
* VL Gothic, a Japanese TrueType font.
* DejaVu, a series of popular, free TrueType fonts.
* A SHA512 hash library, used in the login applications to provide naïve hashed password support.
* The popular DMZ CC By-SA mouse cursor theme from Novell.
* `utf8decode.h`, UTF8 decoding tools.
* A port of `glxgears`.
* Various Curses examples by Pradeep Padala.
* A list of PCI vendor and device names.
* A terminal implementation of the game "2048".

Licenses for these tools and libraries can be found [here](docs/thirdparty.md).

The following external libraries and tools are retrieved during the build process:

* `gcc` and `binutils` - For both a cross-compiler and a native port.
* `libpng` - Used extensively by the native graphics library to provide wallpaper and icons.
* `zlib` - Dependency of `libpng`, but also generally useful.
* `freetype` - For rendering text using TrueType fonts.
* `cairo` and `pixman` - For accelerated and managed graphics.
* `ncurses` - Terminal control library, provides `terminfo`.
* Mesa - Implementation of OpenGL (only the software rasterizer is available).
* Vim - Popular text editor.

In addition to the libraries included in the build process, others have been ported or successfully built for とある:

* Lua - Builds as-is
* MuPDF - See [klange/toaru-pdfviewer](https://github.com/klange/toaru-pdfviewer) (library must be cross compiled)
* SDL - See [klange/SDL](https://github.com/klange/SDL)
* `snes9x-sdl` - See [klange/snes9x-sdl](https://github.com/klange/snes9x-sdl)
* Bochs - Should build as-is, but may require modifications.
* Python - A Python port has been built, but is not yet available.

## Screenshots ##

For a historical look at とあるOS, please see [screenshots.md](docs/screenshots.md).

## Testing / Building / Installation ##

Please see [testing.md](docs/testing.md).

## References ##
Here are some tutorials we found useful early on:

* [James M's kernel development tutorials](http://www.jamesmolloy.co.uk/tutorial_html/index.html)
* [Bran's Kernel Development Tutorial](http://www.osdever.net/bkerndev/Docs/basickernel.htm)
* [Skelix's OS tutorial](http://skelix.net/skelixos/index_en.html)

## Licenses ##

This project is released under the terms of the University of Illinois / NCSA Open Source License, an OSI- and FSF-approved, GPL-compatible open source license. The NCSA License is a derivative of the MIT license and the BSD license; it is reproduced here for your convenience:

    Copyright (c) 2011-2013 Kevin Lange.  All rights reserved.

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to
    deal with the Software without restriction, including without limitation the
    rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
    sell copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:
      1. Redistributions of source code must retain the above copyright notice,
         this list of conditions and the following disclaimers.
      2. Redistributions in binary form must reproduce the above copyright
         notice, this list of conditions and the following disclaimers in the
         documentation and/or other materials provided with the distribution.
      3. Neither the names of the ToAruOS Kernel Development Team, Kevin Lange,
         nor the names of its contributors may be used to endorse
         or promote products derived from this Software without specific prior
         written permission.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
    CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
    WITH THE SOFTWARE.

