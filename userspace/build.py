#!/usr/bin/env python2
# -*- coding: utf-8 -*-
"""
JuJu - An automagical build tool for the とあるOS userspace.

Run with:
    python build.py

For information on the build status, run as:
    python build.py status

To clean build products, use:
    python build.py clean

"""

# TODO: Add support for rebuilding individual targets

import os
import subprocess
import sys

TOOLCHAIN_PATH = os.environ['TOOLCHAIN']

class CCompiler(object):
    extension = 'c'
    compiler  = 'i686-pc-toaru-gcc'
    dependency_hints = {
            '<cairo.h>':           ('cairo', '-lcairo', ['<ft2build.h>', '<pixman.h>']),
            '<ft2build.h>':        ('freetype2', '-lfreetype', []),
            '<pixman.h>':          ('pixman-1', '-lpixman-1',    []),
            '<GL/osmesa.h>':       (None, '-lOSMesa', []),
            '<GL/glu.h>':          (None, '-lGLU', []),
            '<ncurses.h>':         ('ncurses', '-lncurses', []),
            '<panel.h>':           (None, '-lpanel', ['<ncurses.h>']),
            '<menu.h>':            (None, '-lmenu', ['<ncurses.h>']),
            '<mpeg2.h>':           ('mpeg2dec', '-lmpeg2', []),
            '<mpeg2convert.h>':    (None, '-lmpeg2convert', ['<mpeg2.h>']),
            '<zlib.h>':            (None, '-lz', ['<math.h>']),
            '<png.h>':             (None, '-lpng', ['<zlib.h>']),
            '<math.h>':            (None, TOOLCHAIN_PATH + '/lib/libm.a', []),
            '"lib/decorations.h"': (None, 'lib/decorations.o', ['"lib/shmemfonts.h"', '"lib/graphics.h"', '"lib/window.h"']),
            '"lib/graphics.h"':    (None, 'lib/graphics.o',    ['<png.h>']),
            '"lib/kbd.h"':         (None, 'lib/kbd.o',         []),
            '"lib/list.h"':        (None, 'lib/list.o',        []),
            '"lib/hashmap.h"':     (None, 'lib/hashmap.o',     ['"lib/list.h"']),
            '"lib/tree.h"':        (None, 'lib/tree.o',        ['"lib/list.h"']),
            '"lib/testing.h"':     (None, 'lib/testing.o',     []),
            '"lib/pthread.h"':     (None, 'lib/pthread.o',     []),
            '"lib/sha2.h"':        (None, 'lib/sha2.o',        []),
            '"lib/pex.h"':         (None, 'lib/pex.o',         []),
            '"lib/shmemfonts.h"':  (None, 'lib/shmemfonts.o',  ['"lib/graphics.h"', '<ft2build.h>']),
            '"lib/wcwidth.h"':     (None, 'lib/wcwidth.o',     []),
            '"lib/window.h"':      (None, 'lib/window.o',      ['"lib/pthread.h"', '"lib/list.h"']),
            '"lib/yutani.h"':      (None, 'lib/yutani.o',      ['"lib/pthread.h"', '"lib/list.h"', '"lib/pex.h"', '"lib/graphics.h"', '"lib/hashmap.h"']),
            '"gui/ttk/ttk.h"':     (None, 'gui/ttk/lib/ttk-core.o', ['"lib/decorations.h"', '<cairo.h>', '<math.h>']),
            '"gui/terminal/lib/termemu.h"':
                                   (None, 'gui/terminal/lib/termemu.o', []),
    }

    def __init__(self, filename):
        self.filename  = filename
        self.arguments = ['-std=c99', '-U__STRICT_ANSI__', '-O3', '-m32', '-Wa,--32', '-g', '-I.']
        if '/lib/' in filename:
            self.includes, _ = self._depends()
            self.libs = []
            self.arguments.extend(self.includes)
            self.arguments.extend(['-c', '-o', self.output_file(), filename])
        else:
            self.includes, self.libs = self._depends()
            self.arguments.extend(self.includes)
            self.arguments.extend(['-flto', '-o', self.output_file(), filename])
            self.arguments.extend(self.libs)

    def dependencies(self):
        return [os.path.abspath(x) for x in self.libs if 'lib/' in x and x.endswith('.o')]

    def output_file(self):
        if '/lib/' in self.filename:
            return os.path.abspath(self.filename.replace('.c','.o'))
        else:
            return os.path.abspath('../hdd/bin/' + os.path.basename(self.filename).replace('.c',''))

    def _calculate(self, depends, new):
        for k in new:
            if not k in depends:
                depends.append(k)
                _, _, other = self.dependency_hints[k]
                depends = self._calculate(depends, other)
        return depends

    def _depends(self):
        lines = []
        depends = []
        with open(self.filename) as f:
            lines = f.readlines()
        for l in lines:
            if l.startswith('#include'):
                depends.extend([k for k in self.dependency_hints.keys() if l.startswith('#include ' + k)])
        depends = self._calculate([], depends)
        includes  = []
        libraries = []
        for k in depends:
            dep = self.dependency_hints[k]
            if dep[0]:
                includes.append('-I' + TOOLCHAIN_PATH + '/include/' + dep[0])
            if dep[1]:
                libraries.append(dep[1])
        return includes, libraries

    def run(self):
        cmd = [self.compiler]
        cmd.extend(self.arguments)
        self.notify_start()
        ret = subprocess.call(cmd)
        #if not '/lib/' in self.filename:
        #	subprocess.call(['i686-pc-toaru-strip', self.output_file()])
        self.notify_done()
        if ret:
            print target, ret
            print cmd
            raise Exception

    def notify_start(self):
        subprocess.call(['../util/mk-beg', 'CC', self.filename])
    def notify_done(self):
        subprocess.call(['../util/mk-end', 'CC', self.filename])

class CXXCompiler(CCompiler):
    extension = 'cpp'
    compiler  = 'i686-pc-toaru-g++'

    def __init__(self, filename):
        self.filename  = filename
        self.arguments = ['-O3', '-m32', '-Wa,--32', '-g', '-I.']
        if '/lib/' in filename:
            self.includes, _ = self._depends()
            self.libs = []
            self.arguments.extend(self.includes)
            self.arguments.extend(['-c', '-o', self.output_file(), filename])
        else:
            self.includes, self.libs = self._depends()
            self.arguments.extend(self.includes)
            self.arguments.extend(['-flto', '-o', self.output_file(), filename])
            self.arguments.extend(self.libs)

    def output_file(self):
        if '/lib/' in self.filename:
            return os.path.abspath(self.filename.replace('.cpp','.o'))
        else:
            return os.path.abspath('../hdd/bin/' + os.path.basename(self.filename).replace('.cpp',''))

    def notify_start(self):
        subprocess.call(['../util/mk-beg', 'CPP', self.filename])
    def notify_done(self):
        subprocess.call(['../util/mk-end', 'CPP', self.filename])

source_extensions = {'c': CCompiler, 'cpp': CXXCompiler}

def find_sources(path):
    sources = []
    for directory, subdirectories, files in os.walk(path):
        for f in files:
            for k, v in source_extensions.iteritems():
                if f.endswith('.' + k):
                    sources.append((directory + '/' + f, v))
    return sources

sources = {}
for source, compiler in find_sources('.'):
    sources[os.path.abspath(source)] = compiler(source)

outputs   = {}
outputs_r = {}
for k,v in sources.iteritems():
    outputs[k]   = v.output_file()
    outputs_r[outputs[k]] = k

marked = []
rounds = []
remaining = outputs_r.keys()

def satisfied():
    return len(remaining) == 0

while not satisfied():
    this_round = []
    for target in [x for x in outputs_r.keys() if x not in marked]:
        x = sources[outputs_r[target]]
        if all([(y in marked) for y in x.dependencies()]):
            marked.append(target)
            remaining.remove(target)
            this_round.append(target)
        else:
            for y in [dep for dep in x.dependencies() if not dep in marked]:
                if not y in remaining:
                    remaining.append(y)
    if not this_round and not satisfied():
        raise Exception("Dependency resolution error.")
    rounds.append(this_round)

needs_rebuild = []
for r in rounds:
    for target in r:
        source     = outputs_r[target]
        source_obj = sources[source]
        if not os.path.exists(target):
            needs_rebuild.append(target)
        elif os.path.getmtime(target) < os.path.getmtime(source):
            needs_rebuild.append(target)
        elif [True for x in source_obj.dependencies() if x in needs_rebuild]:
            needs_rebuild.append(target)
        elif [True for x in source_obj.dependencies() if os.path.exists(target) and (os.path.getmtime(target) < os.path.getmtime(x))]:
            needs_rebuild.append(target)

if len(sys.argv) > 1 and sys.argv[1] == 'status':
    if not needs_rebuild:
        print "Nothing to do."
    elif len(needs_rebuild) == 1:
        print "One file needs rebuilding."
    else:
        print len(needs_rebuild), "file(s) need rebuilding."
elif len(sys.argv) > 1 and sys.argv[1] == 'clean':
    for i in outputs_r.keys():
        subprocess.call(['../util/mk-beg-rm', 'RM', i])
        subprocess.call(['rm', i])
        subprocess.call(['../util/mk-end-rm', 'RM', i])
else:
    if not needs_rebuild:
        print "Nothing to do."
    else:
        for target in needs_rebuild:
            source_obj = sources[outputs_r[target]]
            source_obj.run()

