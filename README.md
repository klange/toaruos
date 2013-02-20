# ToAruOS (とあるOS) #
![ToAruOS (logo)](https://github.com/klange/osdev/raw/master/docs/logo.png)

とあるOS is a hobby operating system based loosely on POSIX. Development began in December of 2010 at the University of Illinois at Urbana-Champaign, was managed for a short time by the UIUC [SIGOps](http://www.acm.uiuc.edu/sigops/) and, as of May 2012, is once again independently managed. The primary goal of the project is to build a usable operating system and allow its developers to learn the inner workings of various technologies related to that goal.

### Build Status ###

Automated build tests are run with nearly every commit through Travis CI. Sometimes builds time out while downloading external resources, so these results are not always accurate.

[![Automated build testing provided by Travis CI.](https://secure.travis-ci.org/klange/osdev.png?branch=master)](http://travis-ci.org/klange/osdev)

### IRC ###

For discussion, help with building or running the OS, and for up-to-date build verification, please join us in `#toaruos` on Freenode (`irc.freenode.net`).

## Features ##

とあるOS currently supports a number of important operating system concepts and facilities:

* Processes
  * Preemptive multitasking
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
  * Windowed GUI environment with transparency support
  * Limited GUI toolkit available
* Extensive, ANSI-capable terminal
  * Includes extensive compatibility with xterm
  * Support for 256 colors
  * Beautifully rendered anti-aliased text through FreeType (please see the licenses below for included fonts)
  * Supports Unicode text (UTF-8 encoded)
* And dozens of other minor things not worth listing here.

### Third-Party Software ###

While とあるOS ships with only its own native software tools, we are working on packages for additional third-party software, including:

* Lua
  * Lua's standalone interpreter has been successfully built and run.
  * Some Lua functionality is currently missing and we are working to solve this.

The following libraries are built by the build scripts:

* `libpng` - Used extensively by the native graphics library to provide wallpaper and icons.
* `zlib` - Dependency of `libpng`, but also generally useful.
* `freetype` - For rendering text
* `cairo` and `pixman` - For accelerated and managed graphics.

## Screenshots ##

Here's what とあるOS looks like:

![Screenshot](http://i3.minus.com/i2zo4p43c9x1T.png)
(http://i3.minus.com/i2zo4p43c9x1T.png)

Here's what とあるOS has looked like in the past:

* [Previous primary screenshot](http://i.imgur.com/kOpJ2l.png)
* [Running a Cairo demo](http://i.imgur.com/DQK7P.png)
* [With multiple transparent terminals](http://i.imgur.com/K6nPC.png)
* [A terminal and the native editor](http://i.imgur.com/4SGhd.png)
* [Running the  native editor in a full-screen terminal](http://i.imgur.com/u1DEX.png)
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

You can then run `make run` or `make kvm` to run QEMU. An additional `make run-config` command is available that allows easy customization of the emulation environment.

While we only officially support QEMU, other environments - including actual hardware - should work given the appropriate tools. If you are eager to try とあるOS from a real machine, please use GRUB 2 and ensure that you provide the correct graphics payload options. In the future, GRUB 2 configurations will be supported with configuration files and build scripts.

### User Accounts ###

The default root password is `toor`. There is also a regular user `local` with password `local`. While general system security is lacking, most system-modifying calls to the kernel require root privileges. When booting directly to a graphical terminal, the system will run in single-user mode and automatically log in as root. When running in graphical or VGA-terminal mode, a login screen is presented.

### Cygwin ###
With some work, とあるOS is also known to build successfully under cygwin. Instructions are forthcoming.

### Prebuilt Images ###
Our [downloads](https://github.com/klange/osdev/downloads) section contains prebuilt images in the form of VMDK disk images. These disk images should run in VirtualBox, and can be converted to a format suitable for use with QEMU or Bochs. VirtualBox is the preferred environment for non-Linux hosts as it provides hardware virtualization that greatly improves user experience. Be sure to equip your VM with a sufficient amount of RAM (1GB should suffice).

## Dependencies ##

### Kernel ###
To build the kernel, you will need `yasm`, `clang`, and GNU `ld` (which you undoubtedly have if you have `clang` or `gcc`).

### Hard disks ###
You will need `genext2fs` to generate the EXT2 images for the hard disk drive.

## References ##
Here are some tutorials we found useful early on:

* [James M's kernel development tutorials](http://www.jamesmolloy.co.uk/tutorial_html/index.html)
* [Bran's Kernel Development Tutorial](http://www.osdever.net/bkerndev/Docs/basickernel.htm)
* [Skelix's OS tutorial](http://skelix.net/skelixos/index_en.html)

# Licenses #

## ToAruOS Itself ##

This project is released under the terms of the University of Illinois / NCSA Open Source License, an OSI- and FSF-approved, GPL-compatible open source license. The NCSA License is a derivative of the MIT license and the BSD license; it is reproduced here for your convenience:

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

* The CPU detection code provided in `userspace/cpudet.c` carries the following copyright notice, and has been made available by its author for use in other projects:
  Copyright (c) 2006-2007 -  http://brynet.biz.tm - <brynet@gmail.com>

* A copy of the [VL Gothic TrueType font](http://vlgothic.dicey.org/) is included in this repository; VL Gothic is distributed under the following license:

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

* As of January 23, 2012, the repository also contains the [DejaVu fonts](http://dejavu-fonts.org/wiki/Main_Page) package, which is a set of public-domain modifications of the Bitstream Vera font set, which is released under this license:

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

* `userspace/lib/wcwidth.c` is an implementation of the `wcwidth()` non-standard C function by Markus Kuhn, it is provided with the following license:

        Markus Kuhn -- 2007-05-26 (Unicode 5.0)

        Permission to use, copy, modify, and distribute this software
        for any purpose and without fee is hereby granted. The author
        disclaims all warranties with regard to this software.

        Latest version: http://www.cl.cam.ac.uk/~mgk25/ucs/wcwidth.c

* The code to perform a Gaussian blur on a graphics context (part of userspace/lib/graphics.c) comes from sample code for Cairo and carries the following license:

        Copyright © 2008 Kristian Høgsberg
        Copyright © 2009 Chris Wilson

        Permission to use, copy, modify, distribute, and sell this software and its
        documentation for any purpose is hereby granted without fee, provided that
        the above copyright notice appear in all copies and that both that copyright
        notice and this permission notice appear in supporting documentation, and
        that the name of the copyright holders not be used in advertising or
        publicity pertaining to distribution of the software without specific,
        written prior permission.  The copyright holders make no representations
        about the suitability of this software for any purpose.  It is provided "as
        is" without express or implied warranty.

        THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
        INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
        EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
        CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
        DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
        TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
        OF THIS SOFTWARE.

* Some icons from the Elementary Icons set are included. These are icons are released under the GPL. You may find a list of authors and contributors alongside the icon files.

* Build scripts will retrieve copies of [GCC](http://gcc.gnu.org/), [Newlib](http://sourceware.org/newlib/), [FreeType](http://www.freetype.org/), [libpng](http://www.libpng.org/pub/png/libpng.html), [zlib](http://www.zlib.net/), and [Cairo](http://www.cairographics.org/). Patches for these software packages are provided under the same license as the package they are for.<F7>

