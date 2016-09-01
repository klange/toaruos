![Screenshot](http://i.imgur.com/oeGNxX7.png)
![More Screenshots](http://i.imgur.com/mogzwiO.png)

# ToaruOS (とあるOS) #

とあるOS (ToaruOS) is a hobby operating system built mostly from scratch, including both a kernel and userspace.

This repository contains the kernel, modules, and core userspace applications and libraries. Some third-party libraries and utilities are required to build a working system.

## Build Instructions ##

Please ensure you are running [a supported environment](https://github.com/klange/toaruos/wiki/Testing-and-Building#requirements) before continuing.

Start by building a toolchain:

    make toolchain

You may be prompted to enter your password for `sudo` to install required packages. The toolchain build scripts will then build several dependencies for building ToaruOS such as a GCC cross compiler and a few libraries used by the userspace. This may take around thirty minutes to an hour to complete, depending on your hardware. Once it is done, verify there were no errors and then follow the instructions provided to continue with the build.

If you experience issues, please join our [IRC channel](#irc) for help.

## History ##

ToaruOS started as a side project at the University of Illinois at Urbana-Champaign. For several months in late 2011 and early 2012, the University's [SIGOps](http://www.acm.uiuc.edu/sigops/) chapter managed development efforts focused on building the original compositing GUI. Since then, the project has mostly been a one-man effort with a handful of third party contributions.

## Kernel ##

The Toaru kernel provides a basic Unix-like environment. The kernel uses a hybrid modular architecture, with loadable modules providing most device driver support. The core kernel includes support for Unix pipes and TTYs, a virtual file system, multitasking, ELF binary support, and various core platform features on x86 systems.

Modules provide support for disk drives, ext2 filesystems, serial, keyboards and mice, a `/proc` filesystem similar to the one found in Linux, as well as an expanding collection of other device drivers.

## Userspace ##

ToaruOS's userspace is focused on a rich graphical environment, backed by an in-house compositing window manager. ToaruOS's terminal emulator supports xterm-compatible 256-color modes, as well as Konsole 24-bit color modes and anti-aliased text with basic Unicode support. Several graphical demos are provided, alongside a number of command-line applications. A port of SDL targetting the native graphical environment is also available.

### Third-Party Software ###

The userspace depends on a number of third-party libraries which are outside of the development scope of the project. Additionally, several third-party applications and libraries have been integrated into ToaruOS's core userspace, or otherwise ported to ToaruOS.

Package |   | Description
------- | ---- | -----------
cpudet| *(included)* | `cpuid` parser
DejaVu Sans | *(included)* | Popular, free TrueType font series
VL Gothic | *(included)* | Free Japanese TrueType font
`sha2.c` | *(included)* | SHA512 hash library
`utf8decode.h` | *(included)* | UTF8 decoding tools
glxgears | *(included)* | Popular OpenGL demo
curses-hello | *(included)* | Curses demos
`pci_list.h` | *(included)* | PCI vendor and device names
2048  | *(included)*    | Terminal implementation of a popular strategy game
gcc   | \*   | Compiler suite.
newlib| \*   | C library
libpng| \*   | PNG graphics library
zlib  | \*   | `gzip` compression library
FreeType | \* | TrueType font parser
Cairo | \*   | CPU-accelerated pixel-pushing and vector graphics
ncurses | \* | Terminal UI library and `terminfo` provider
Mesa | \* | Software OpenGL implementation
Vim | \* | Popular text editor
Lua  |  [link](http://www.lua.org/) | Interpreted programming language
MuPDF | [link](https://github.com/klange/toaru-pdfviewer) | PDF viewer (requires complex library cross-compilation)
SDL | [link](https://github.com/klange/SDL) | Cross-platform graphics library
Snes9X | [link](https://github.com/klange/snes9x-sdl) | SNES emulator
PrBoom | [link](https://github.com/klange/prboom) | DooM engine
Bochs | [link](http://bochs.sourceforge.net/) | Software x86 PC emulator
Python | (TBD) | Interpreted programming language

\* These tools and libraries are retrieved by the build process and included by default. Some of them are dependencies for the core userspace.

License for the included third-party tools and libraries can be found [here](LICENSE.md).

## Community ##

### Wiki ###

For additional screenshots, see [Screenshots](https://github.com/klange/toaruos/wiki/Screenshots).

For instructions on building, see [Testing and Building](https://github.com/klange/toaruos/wiki/Testing-and-Building).

### IRC ###

For help building the kernel and userspace, join us in `#toaruos` on Freenode (`irc.freenode.net`).


