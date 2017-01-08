#!/usr/bin/env python3
# coding: utf-8

import os
import sys

try:
    TOOLCHAIN_PATH = os.environ['TOOLCHAIN']
except KeyError:
    # This is not good, but we need to let it happen for the make file
    TOOLCHAIN_PATH = ""

force_static = ("OSMesa","GLU")

class Classifier(object):

    dependency_hints = {
        # Core libraries
        '<math.h>':            (None, '-lm', []),
        '<cairo.h>':           ('cairo', '-lcairo', ['<ft2build.h>', '<pixman.h>', '<png.h>']),
        '<ft2build.h>':        ('freetype2', '-lfreetype', ['<zlib.h>']),
        '<pixman.h>':          ('pixman-1', '-lpixman-1', ['<math.h>']),
        '<GL/osmesa.h>':       (None, '-lOSMesa', []),
        '<GL/glu.h>':          (None, '-lGLU', []),
        '<ncurses.h>':         ('ncurses', '-lncurses', []),
        '<panel.h>':           (None, '-lpanel', ['<ncurses.h>']),
        '<menu.h>':            (None, '-lmenu', ['<ncurses.h>']),
        '<zlib.h>':            (None, '-lz', ['<math.h>']),
        '<png.h>':             (None, '-lpng15', ['<zlib.h>']),
        '<Python.h>':          ('python/include/python3.6m', '-lpython3.6m', ['<math.h>']),
        # Toaru Standard Library
        '<toaru.h>':           (None, '-ltoaru', ['<png.h>','<ft2build.h>','<cairo.h>']),
        '"lib/toaru_auth.h"':  (None, '-ltoaru-toaru_auth',  ['"lib/sha2.h"']),
        '"lib/kbd.h"':         (None, '-ltoaru-kbd',         []),
        '"lib/list.h"':        (None, '-ltoaru-list',        []),
        '"lib/hashmap.h"':     (None, '-ltoaru-hashmap',     ['"lib/list.h"']),
        '"lib/tree.h"':        (None, '-ltoaru-tree',        ['"lib/list.h"']),
        '"lib/testing.h"':     (None, '-ltoaru-testing',     []),
        '"lib/pthread.h"':     (None, '-ltoaru-pthread',     []),
        '"lib/sha2.h"':        (None, '-ltoaru-sha2',        []),
        '"lib/pex.h"':         (None, '-ltoaru-pex',         []),
        '"lib/graphics.h"':    (None, '-ltoaru-graphics',    ['<png.h>']),
        '"lib/shmemfonts.h"':  (None, '-ltoaru-shmemfonts',  ['"lib/graphics.h"', '<ft2build.h>']),
        '"lib/rline.h"':       (None, '-ltoaru-rline',       ['"lib/kbd.h"']),
        '"lib/confreader.h"':  (None, '-ltoaru-confreader',  ['"lib/hashmap.h"']),
        '"lib/network.h"':     (None, '-ltoaru-network',     []),
        '"lib/http_parser.h"': (None, '-ltoaru-http_parser', []),
        '"lib/dlfcn.h"':       (None, '-ltoaru-dlfcn',       []),
        # Yutani Libraries
        '"lib/yutani.h"':      (None, '-ltoaru-yutani',      ['"lib/kbd.h"', '"lib/list.h"', '"lib/pex.h"', '"lib/graphics.h"', '"lib/hashmap.h"']),
        '"lib/decorations.h"': (None, '-ltoaru-decorations', ['"lib/shmemfonts.h"', '"lib/graphics.h"', '"lib/yutani.h"','"lib/dlfcn.h"']),
        '"gui/ttk/ttk.h"':     (None, '-ltoaru-ttk', ['"lib/decorations.h"', '"lib/hashmap.h"',  '<cairo.h>', '<math.h>']),
        '"gui/terminal/lib/termemu.h"': (None, '-ltoaru-termemu', ['"lib/graphics.h"']),
    }

    def __init__(self, filename):
        self.filename  = filename
        self.includes, self.libs = self._depends()

    def _calculate(self, depends, new):
        """Calculate all dependencies for the given set of new elements."""
        for k in new:
            if not k in depends:
                depends.append(k)
                _, _, other = self.dependency_hints[k]
                depends = self._calculate(depends, other)
        return depends

    def _sort(self, depends):
        """Sort the list of dependencies so that elements appearing first depend on elements following."""
        satisfied = []
        a = depends[:]

        while set(satisfied) != set(depends):
            b = []
            for k in a:
                if all([x in satisfied for x in self.dependency_hints[k][2]]):
                    satisfied.append(k)
                else:
                    b.append(k)
            a = b[:]
        return satisfied[::-1]

    def _depends(self):
        """Calculate include and library dependencies."""
        lines = []
        depends = []
        with open(self.filename,'r') as f:
            lines = f.readlines()
        for l in lines:
            if l.startswith('#include'):
                depends.extend([k for k in list(self.dependency_hints.keys()) if l.startswith('#include ' + k)])
        depends = self._calculate([], depends)
        depends = self._sort(depends)
        includes  = []
        libraries = []
        for k in depends:
            dep = self.dependency_hints[k]
            if dep[0]:
                includes.append('-I' + TOOLCHAIN_PATH + '/include/' + dep[0])
            if dep[1]:
                libraries.append(dep[1])
        return includes, libraries


def todep(name):
    """Convert a library name to an archive path or object file name."""
    if name.startswith("-l"):
        name = name.replace("-l","",1)
        if name in force_static:
            return (False, "%s/lib%s.a" % (TOOLCHAIN_PATH + '/lib', name))
        else:
            return (True, "%s/lib%s.so" % ('hdd/usr/lib', name))
    else:
        return (False, name)

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("usage: util/auto-dep.py command filename")
        exit(1)
    command  = sys.argv[1]
    filename = sys.argv[2]

    c = Classifier(filename)

    if command == "--cflags":
        print(" ".join([x for x in c.includes]))
    elif command == "--libs":
        print(" ".join([x for x in c.libs]))
    elif command == "--deps":
        results = [todep(x) for x in c.libs]
        normal = [x[1] for x in results if not x[0]]
        order_only = [x[1] for x in results if x[0]]
        print(" ".join(normal) + " | " + " ".join(order_only))
