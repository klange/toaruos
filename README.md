# ToAruOS (とあるOS) #
![ToAruOS (logo)](https://github.com/klange/osdev/raw/master/docs/logo.png)

This is a toy OS based on the POSIX standards. The primary goal of the project is for me to learn POSIX from the system side, understanding the design and constructon of an operating system on x86 hardware, and build a working implementation of the C standard library. Development of とあるOS was managed by the UIUC [SIGOps](http://www.acm.uiuc.edu/sigops/) until May, 2012. While the ultimate goal is a microkernel, we currently have a relatively monolithic kernel.

### Build Status ###

[![Automated build testing provided by Travis CI.](https://secure.travis-ci.org/klange/osdev.png?branch=master)](http://travis-ci.org/klange/osdev)

## News ##

We are currently working on porting a toolchain to natively run under とあるOS. We currently have a nearly-working build of binutils and are working on porting gcc.

## Features ##

とあるOS currently supports a number of important operating system concepts and facilities:

* Processes
  * Preemptive multasking
  * Kernel threads
* Shared memory
  * Through page table mappings
* POSIX-compliant file operations
* Pipes
  * Backed by ring buffers
* Signals
  * Mostly POSIX-compliant under the loose method
* EXT2 read/write support
  * For IDE devices or SATA devices *operating in IDE compatibility mode*
* PS/2 keyboard/mouse support
  * Of course, what doesn't?
* Graphical features
  * We are hard at work building a window environment that makes use of shared memory
  * Currently supports full-screen operation of graphical applications
* Extensive, ANSI-capable terminal
  * Includes extensive compatibility with xterm
  * Support for 256 colors
  * Beautifully rendered anti-aliased text through FreeType (please see the licenses below for included fonts)
* And dozens of other minor things not worth listing here.

## Screenshots ##

Here's what とあるOS looks like:

![Screenshot](http://i.imgur.com/Xn93Pl.png)

(http://i.imgur.com/Xn93P.png)

Here's what とあるOS has looked like in the past:

* [Fully userspace shell, demonstrating a threading application](http://i.imgur.com/S7rab.png)
* [Logging in and moving around](http://i.imgur.com/VjTMv.png)
* [Booting up the (WIP) graphical interface](http://i.imgur.com/g2yfr.png)
* [Nyaning](http://i.imgur.com/2BISA.png)
* [Demoing multitasking](http://i.imgur.com/RY4lb.png)

* [Empty kernel shell](http://i.imgur.com/44huV.jpg)
* [With a wallpaper and mouse cursor](http://i.imgur.com/eG0i9.png)
* [Running some GNU apps](http://i.imgur.com/aKyJb.png)
* [Playing a simple RPG](http://i.imgur.com/A7csE.jpg)
* [Generating Julia fractals](http://i.imgur.com/Uy3JJ.jpg)
* [Early boot screen testing and some old serial output](http://i.imgur.com/1CMJ0.jpg)

## Testing it Out ##
If you are on Debian/Ubuntu/etc., you can clone the repository and run:

    ./build.sh

This will install the required dependencies, build the userspace libraries and toolchain, build the kernel, and give you a working ramdisk and hard disk.

You can then run `make run` or `make kvm` to run QEMU, or `make docs` to build documentation (which requires a LaTeX stack with CJK support).

Currently, the only supported environment is QEMU, as our limited graphics drivers do not operate on most real hardware.

## Dependencies ##

### Kernel ###
To build the kernel, you will need `yasm`, `clang` (or `gcc`, the build tools will autodetect), and GNU `ld` (which you undoubtedly have if you have `clang` or `gcc`).

### Hard disks and initrds ###
You need `genext2fs` to generate the EXT2 images for the ramdisk and the hard disk drive.

### Documentation ###
To build the primary documentation, you need a complete LaTeX stack with `pdftex`, including the CJK module and Japanese fonts. To build the kernel API documentation, you will need Doxygen (eventually).

# Goals and Future Development #

### Please note that much of this section is outdated. ###

## Goals and Roadmap ##
Overall, the goal of this project is to write a relatively POSIX-compatible OS from the ground up. With a focus on generic hardware functionality and universal specifications like VESA, I hope to eventually get something fairly complete in terms of what an OS should be. At some times, I may focus on an actual piece of complex hardware (I am looking to write a basic driver for Intel graphics cards based on the X driver and the Mesa components), but in general, I will stick to generic interfaces.

### Basic Goals ###
* Create a working modular monolithic kernel capable of executing arbitrary ELF binaries
* Write, from scratch, a C standard library using past experience in writing standard library functions
* Support POSIX threads
* Implement an existing file system, specifically EXT2
* Be able to manipulate VESA modes to run at an optimal resolution for graphics
* Handle basic networking on a virtual Ethernet device (DHCP, TCP, etc.)

### Loftier Goals ###
Some things are far easier said than done, but I like to say them anyway. The time span for these depends greatly on how quickly the basic goals are completed and can range anywhere from a few months to years from now.

* Dynamic library loader
* Create a working implementation of Wayland under VESA (will be slow)
* Port Qt (under Wayland) and some Qt apps
    * Port Qt under framebuffer first? Qt has everything...
* Support audio in a way that doesn't suck like Linux's mess of libraries and mixers
* Various hardware-specific drivers (primarily for my T410):
    * Intel graphics driver, with acceleration so Wayland isn't slow
    * Realtek wireless driver, with WPA2
    * Specific drivers for the Thinkpad itself (or just acpi?)
* Custom b-tree filesystem

### Roadmap ###
Currently, I have a kernel capable of reading its multiboot parameters, which is terribly un-useful. The current, ordered, plan of attack is as follows:

* Finish James M's tutorial (second half), which covers:
    * Paging *done*
    * Heap *done*
    * VFS *done*
    * Initial RAM Disk *done*
    * Multitasking *done*
    * User mode *done*
* Finish basic kernel functionality
    * Loading ELF binaries and executing them in user mode *done (static)*
    * Complete system call table *good*
    * Get a better semblance of users and groups *we have users*
* Write a file system driver for a real file system
    * Target is EXT2, but might do FAT *IDE PIO support*
    * Move OS development images to some form of virtual drive *done*
* Implement a VESA mode handler
    * QEMU / BOCHS VBE driver *done*
    * Requires a Virtual 8086 monitor *emulated*
    * Need to be able to use graphics modes and still have output, so write a framebuffer terminal *done*
* Complete libc
    * Enough to run basic unix tools... *basically done*
* Port some basic UNIX tools
    * a shell (bash and zsh, because I like bash, but the office uses zsh) *esh, the "experimental shell"*
    * *ls*, mv, rm, etc.
    * here's a real test: perl
* Implement networking
    * IPv4
    * Ethernet driver for QEMU or VirtualBox

*Anything beyond this point is part of the 'Loftier Goals' section*

* Wayland compositor
    * based on specifications for a Wayland environment
    * Port some of the Wayland sample applications
    * Write my own!
* Port Qt
    * Specifically, for Wayland
    * Qt is huge and has its own standard library, might need more extensive libc
    * Need to support C++-built stuff, so will probably need a C++ stdlib.
* Audio drivers
    * Maybe before Qt?
    * Should support software mixing at least, hardware under a virtual machine, maybe my Intel hw

## References ##
Here are some tutorials we found useful early on:

* [James M's kernel development tutorials](http://www.jamesmolloy.co.uk/tutorial_html/index.html)
* [Bran's Kernel Development Tutorial](http://www.osdever.net/bkerndev/)
* [Skelix's OS tutorial](http://en.skelix.org/skelixos/)

# Licenses #

## ToAruOS Itself ##

ToAruOS is under the NCSA license, which is a derivative (and fully compatible with) the BSD license. It is also forward compatible with the GPL, so you can use ToAruOS bits and pieces in GPL. The terms of the license are listed here for your convience:

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

## Additional Licenses ##

ToAruOS contains additional software with the following copyright notices:

* The CPU detection code provided in `userspace/cpudet.c`:
  Copyright (c) 2006-2007 -  http://brynet.biz.tm - <brynet@gmail.com>

* A copy of the VL Gothic TrueType font is included in this repository; VL Gothic is distributed under the following license:

        Copyright (c) 1990-2003 Wada Laboratory, the University of Tokyo.
        Copyright (c) 2003-2004 Electronic Font Open Laboratory (/efont/).
        Copyright (C) 2002-2010 M+ FONTS PROJECT Copyright (C) 2006-2010
        Daisuke SUZUKI .  Copyright (C) 2006-2010 Project Vine .  All rights
        reserved.

        Redistribution and use in source and binary forms, with or without
        modification, are permitted provided that the following conditions are
        met:
        1. Redistributions of source code must retain the above copyright
           notice, this list of conditions and the following disclaimer.
        2. Redistributions in binary form must reproduce the above copyright
           notice, this list of conditions and the following disclaimer in the
           documentation and/or other materials provided with the distribution.
        3. Neither the name of the Wada Laboratory, the University of Tokyo nor
           the names of its contributors may be used to endorse or promote
           products derived from this software without specific prior written
           permission.

        THIS SOFTWARE IS PROVIDED BY WADA LABORATORY, THE UNIVERSITY OF TOKYO
        AND CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
        INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
        MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
        IN NO EVENT SHALL THE LABORATORY OR CONTRIBUTORS BE LIABLE FOR ANY
        DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
        DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
        OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
        HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
        STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
        IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
        POSSIBILITY OF SUCH DAMAGE.

* The real-mode 8086 emulation provided for VESA support is distributed under the following license:

        Copyright 2010 John Hodge (thePowersGang). All rights reserved.

        Redistribution and use in source and binary forms, with or without
        modification, are permitted provided that the following conditions are
        met:

           1. Redistributions of source code must retain the above copyright
              notice, this list of conditions and the following disclaimer.

           2. Redistributions in binary form must reproduce the above copyright
              notice, this list of conditions and the following disclaimer in
              the documentation and/or other materials provided with the
              distribution.

        THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND ANY
        EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
        IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
        PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> OR
        CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
        EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
        PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
        PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
        LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
        NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
        SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

        The views and conclusions contained in the software and documentation
        are those of the authors and should not be interpreted as representing
        official policies, either expressed or implied, of the author.

* As of January 23, 2012, the repository also contains DejaVu fonts, which are public-domain modifications of the Bitstream Vera font set, which is released under this license:

        Copyright (c) 2003 by Bitstream, Inc. All Rights Reserved. Bitstream
        Vera is a trademark of Bitstream, Inc.

        Permission is hereby granted, free of charge, to any person obtaining a
        copy of the fonts accompanying this license ("Fonts") and associated
        documentation files (the "Font Software"), to reproduce and distribute
        the Font Software, including without limitation the rights to use,
        copy, merge, publish, distribute, and/or sell copies of the Font
        Software, and to permit persons to whom the Font Software is furnished
        to do so, subject to the following conditions:

        The above copyright and trademark notices and this permission notice
        shall be included in all copies of one or more of the Font Software
        typefaces.

        The Font Software may be modified, altered, or added to, and in
        particular the designs of glyphs or characters in the Fonts may be
        modified and additional glyphs or characters may be added to the Fonts,
        only if the fonts are renamed to names not containing either the words
        "Bitstream" or the word "Vera".

        This License becomes null and void to the extent applicable to Fonts or
        Font Software that has been modified and is distributed under the
        "Bitstream Vera" names.

        The Font Software may be sold as part of a larger software package but
        no copy of one or more of the Font Software typefaces may be sold by
        itself.

        THE FONT SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
        EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO ANY WARRANTIES OF
        MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT
        OF COPYRIGHT, PATENT, TRADEMARK, OR OTHER RIGHT. IN NO EVENT SHALL
        BITSTREAM OR THE GNOME FOUNDATION BE LIABLE FOR ANY CLAIM, DAMAGES OR
        OTHER LIABILITY, INCLUDING ANY GENERAL, SPECIAL, INDIRECT, INCIDENTAL,
        OR CONSEQUENTIAL DAMAGES, WHETHER IN AN ACTION OF CONTRACT, TORT OR
        OTHERWISE, ARISING FROM, OUT OF THE USE OR INABILITY TO USE THE FONT
        SOFTWARE OR FROM OTHER DEALINGS IN THE FONT SOFTWARE.

        Except as contained in this notice, the names of Gnome, the Gnome
        Foundation, and Bitstream Inc., shall not be used in advertising or
        otherwise to promote the sale, use or other dealings in this Font
        Software without prior written authorization from the Gnome Foundation
        or Bitstream Inc., respectively. For further information, contact:
        fonts at gnome dot org.

* The included SHA512 support library (userspace/lib/sha2.{c,h}) is provided under the BSD license as follows:

        Copyright (c) 2000-2001, Aaron D. Gifford
        All rights reserved.

        Redistribution and use in source and binary forms, with or without
        modification, are permitted provided that the following conditions
        are met:
        1. Redistributions of source code must retain the above copyright
           notice, this list of conditions and the following disclaimer.
        2. Redistributions in binary form must reproduce the above copyright
           notice, this list of conditions and the following disclaimer in the
           documentation and/or other materials provided with the distribution.
        3. Neither the name of the copyright holder nor the names of contributors
           may be used to endorse or promote products derived from this software
           without specific prior written permission.

        THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTOR(S) ``AS IS'' AND
        ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
        IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
        ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTOR(S) BE LIABLE
        FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
        DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
        OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
        HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
        LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
        OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
        SUCH DAMAGE.

* The DMZ mouse cursor theme is licensed under the Creative Commons Attribution-ShareAlike 3.0 license with the following copyright notice:

        Copyright © 2007-2010 Novell, Inc.

* Bjoern Hoehrmann's super-simple UTF8 decoder is included for use in userspace applications and is provided under the following license:

       Copyright (c) 2008-2009 Bjoern Hoehrmann <bjoern@hoehrmann.de>

       Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal in
    the Software without restriction, including without limitation the rights to
    use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
    the Software, and to permit persons to whom the Software is furnished to do so,
    subject to the following conditions:

       The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

       THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
    FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
    COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
    IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
    CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

* We ask that anyone who notices unlicensed materials in this repository apply Hanlon's Razor and assume we were idiots and missed it before assuming malicious intent; you may report content for which a license is not provided by emailing us.
