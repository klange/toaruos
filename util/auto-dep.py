#!/usr/bin/env python2
# -*- coding: utf-8 -*-

import os
import sys

try:
    TOOLCHAIN_PATH = os.environ['TOOLCHAIN']
except KeyError:
    # This is not good, but we need to let it happen for the make file
    TOOLCHAIN_PATH = ""

class Classifier(object):

    dependency_hints = {
        # Core libraries
        '<math.h>':            (None, '-lm', []),
        '<cairo.h>':           ('cairo', '-lcairo', ['<ft2build.h>', '<pixman.h>']),
        '<ft2build.h>':        ('freetype2', '-lfreetype', []),
        '<pixman.h>':          ('pixman-1', '-lpixman-1', ['<math.h>']),
        '<GL/osmesa.h>':       (None, '-lOSMesa', []),
        '<GL/glu.h>':          (None, '-lGLU', []),
        '<ncurses.h>':         ('ncurses', '-lncurses', []),
        '<panel.h>':           (None, '-lpanel', ['<ncurses.h>']),
        '<menu.h>':            (None, '-lmenu', ['<ncurses.h>']),
        '<zlib.h>':            (None, '-lz', ['<math.h>']),
        '<png.h>':             (None, '-lpng', ['<zlib.h>']),
        # Toaru Standard Library
        '"lib/toaru_auth.h"':  (None, 'userspace/lib/toaru_auth.o',  ['"lib/sha2.h"']),
        '"lib/kbd.h"':         (None, 'userspace/lib/kbd.o',         []),
        '"lib/list.h"':        (None, 'userspace/lib/list.o',        []),
        '"lib/hashmap.h"':     (None, 'userspace/lib/hashmap.o',     ['"lib/list.h"']),
        '"lib/tree.h"':        (None, 'userspace/lib/tree.o',        ['"lib/list.h"']),
        '"lib/testing.h"':     (None, 'userspace/lib/testing.o',     []),
        '"lib/pthread.h"':     (None, 'userspace/lib/pthread.o',     []),
        '"lib/sha2.h"':        (None, 'userspace/lib/sha2.o',        []),
        '"lib/pex.h"':         (None, 'userspace/lib/pex.o',         []),
        '"lib/graphics.h"':    (None, 'userspace/lib/graphics.o',    ['<png.h>']),
        '"lib/shmemfonts.h"':  (None, 'userspace/lib/shmemfonts.o',  ['"lib/graphics.h"', '<ft2build.h>']),
        '"lib/rline.h"':       (None, 'userspace/lib/rline.o',       ['"lib/kbd.h"']),
        # Yutani Libraries
        '"lib/yutani.h"':      (None, 'userspace/lib/yutani.o',      ['"lib/list.h"', '"lib/pex.h"', '"lib/graphics.h"', '"lib/hashmap.h"']),
        '"lib/decorations.h"': (None, 'userspace/lib/decorations.o', ['"lib/shmemfonts.h"', '"lib/graphics.h"', '"lib/yutani.h"']),
        '"gui/ttk/ttk.h"':     (None, 'userspace/gui/ttk/lib/ttk-core.o', ['"lib/decorations.h"', '"lib/hashmap.h"',  '<cairo.h>', '<math.h>']),
        '"gui/terminal/lib/termemu.h"':
                               (None, 'userspace/gui/terminal/lib/termemu.o', []),
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
        with open(self.filename) as f:
            lines = f.readlines()
        for l in lines:
            if l.startswith('#include'):
                depends.extend([k for k in self.dependency_hints.keys() if l.startswith('#include ' + k)])
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
        return "%s/lib%s.a" % (TOOLCHAIN_PATH + '/lib', name)
    else:
        return name

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print "usage: util/auto-dep.py command filename"
        exit(1)
    command  = sys.argv[1]
    filename = sys.argv[2]

    c = Classifier(filename)

    if command == "--cflags":
        print " ".join([x for x in c.includes])
    elif command == "--libs":
        print " ".join([x for x in c.libs])
    elif command == "--deps":
        print " ".join([todep(x) for x in c.libs])
