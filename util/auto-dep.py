#!/usr/bin/env python3
# coding: utf-8

import os
import sys

import subprocess

cflags = "-O3 -g -std=gnu99 -I. -Iapps -pipe -mmmx -msse -msse2 -fplan9-extensions -Wall -Wextra -Wno-unused-parameter"

class Classifier(object):

    dependency_hints = {
        # Toaru Standard Library
        '<toaru/kbd.h>':         (None, '-ltoaru_kbd',         []),
        '<toaru/list.h>':        (None, '-ltoaru_list',        []),
        '<toaru/hashmap.h>':     (None, '-ltoaru_hashmap',     ['<toaru/list.h>']),
        '<toaru/tree.h>':        (None, '-ltoaru_tree',        ['<toaru/list.h>']),
        '<toaru/pex.h>':         (None, '-ltoaru_pex',         []),
        '<toaru/auth.h>':        (None, '-ltoaru_auth',        []),
        '<toaru/graphics.h>':    (None, '-ltoaru_graphics',    []),
        '<toaru/inflate.h>':     (None, '-ltoaru_inflate',     []),
        '<toaru/drawstring.h>':  (None, '-ltoaru_drawstring',  ['<toaru/graphics.h>']),
        '<toaru/jpeg.h>':        (None, '-ltoaru_jpeg',        ['<toaru/graphics.h>']),
        '<toaru/png.h>':         (None, '-ltoaru_png',         ['<toaru/graphics.h>','<toaru/inflate.h>']),
        '<toaru/rline.h>':       (None, '-ltoaru_rline',       ['<toaru/kbd.h>']),
        '<toaru/rline_exp.h>':   (None, '-ltoaru_rline_exp',   ['<toaru/rline.h>']),
        '<toaru/confreader.h>':  (None, '-ltoaru_confreader',  ['<toaru/hashmap.h>']),
        '<toaru/markup.h>':      (None, '-ltoaru_markup',      ['<toaru/hashmap.h>']),
        '<toaru/json.h>':        (None, '-ltoaru_json',        ['<toaru/hashmap.h>']),
        '<toaru/yutani.h>':      (None, '-ltoaru_yutani',      ['<toaru/kbd.h>', '<toaru/list.h>', '<toaru/pex.h>', '<toaru/graphics.h>', '<toaru/hashmap.h>']),
        '<toaru/decorations.h>': (None, '-ltoaru_decorations', ['<toaru/menu.h>', '<toaru/sdf.h>', '<toaru/graphics.h>', '<toaru/yutani.h>']),
        '<toaru/termemu.h>':     (None, '-ltoaru_termemu',     ['<toaru/graphics.h>']),
        '<toaru/sdf.h>':         (None, '-ltoaru_sdf',         ['<toaru/graphics.h>', '<toaru/hashmap.h>']),
        '<toaru/icon_cache.h>':  (None, '-ltoaru_icon_cache',  ['<toaru/graphics.h>', '<toaru/hashmap.h>']),
        '<toaru/menu.h>':        (None, '-ltoaru_menu',        ['<toaru/sdf.h>', '<toaru/yutani.h>', '<toaru/icon_cache.h>', '<toaru/graphics.h>', '<toaru/hashmap.h>']),
        '<toaru/textregion.h>':  (None, '-ltoaru_textregion',  ['<toaru/sdf.h>', '<toaru/yutani.h>','<toaru/graphics.h>', '<toaru/hashmap.h>']),
        '<toaru/button.h>':      (None, '-ltoaru_button',      ['<toaru/graphics.h>','<toaru/sdf.h>', '<toaru/icon_cache.h>']),
        # OPTIONAL third-party libraries, for extensions / ports
        '<ft2build.h>':        ('freetype2', '-lfreetype', []),
        '<pixman.h>':          ('pixman-1', '-lpixman-1', []),
        '<cairo.h>':           ('cairo', '-lcairo', ['<ft2build.h>', '<pixman.h>']),
    }

    def __init__(self, filename):
        self.export_dynamic_hint = False
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
            elif l.startswith('/* auto-dep: export-dynamic */'):
                self.export_dynamic_hint = True
        depends = self._calculate([], depends)
        depends = self._sort(depends)
        includes  = []
        libraries = []
        for k in depends:
            dep = self.dependency_hints[k]
            if dep[0]:
                includes.append('-I' + 'base/usr/include/' + dep[0])
            if dep[1]:
                libraries.append(dep[1])
        return includes, libraries


def todep(name):
    """Convert a library name to an archive path or object file name."""
    if name.startswith("-l"):
        name = name.replace("-l","",1)
        if name.startswith('toaru'):
            return (True, "%s/lib%s.so" % ('base/lib', name))
        else:
            return (True, "%s/lib%s.so" % ('base/usr/lib', name))
    else:
        return (False, name)

def toheader(name):
    if name.startswith('-ltoaru_'):
        return name.replace('-ltoaru_','base/usr/include/toaru/') + '.h'
    else:
        return ''

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
    elif command == "--build":
        subprocess.run("gcc {cflags} {extra} {includes} -o {app} {source} {libraries}".format(
            cflags=cflags,
            app=os.path.basename(filename).replace(".c",""),
            source=filename,
            headers=" ".join([toheader(x) for x in c.libs]),
            libraries=" ".join([x for x in c.libs]),
            includes=" ".join([x for x in c.includes if x is not None]),
            extra="-Wl,--export-dynamic" if c.export_dynamic_hint else "",
            ), shell=True)
    elif command == "--buildlib":
        libname = os.path.basename(filename).replace(".c","")
        _libs = [x for x in c.libs if not x.startswith('-ltoaru_') or x.replace("-ltoaru_","") != libname]
        subprocess.run("gcc {cflags} {includes} -shared -fPIC -olibtoaru_{lib}.so {source} {libraries}".format(
            cflags=cflags,
            lib=libname,
            source=filename,
            headers=" ".join([toheader(x) for x in c.libs]),
            libraryfiles=" ".join([todep(x)[1] for x in _libs]),
            libraries=" ".join([x for x in _libs]),
            includes=" ".join([x for x in c.includes if x is not None])
            ),shell=True)
    elif command == "--make":
        print("base/bin/{app}: {source} {headers} util/auto-dep.py | {libraryfiles} $(LC)\n\t$(CC) $(CFLAGS) {extra} {includes} -o $@ $< {libraries}".format(
            app=os.path.basename(filename).replace(".c",""),
            source=filename,
            headers=" ".join([toheader(x) for x in c.libs]),
            libraryfiles=" ".join([todep(x)[1] for x in c.libs]),
            libraries=" ".join([x for x in c.libs]),
            includes=" ".join([x for x in c.includes if x is not None]),
            extra="-Wl,--export-dynamic" if c.export_dynamic_hint else "",
            ))
    elif command == "--makelib":
        libname = os.path.basename(filename).replace(".c","")
        _libs = [x for x in c.libs if not x.startswith('-ltoaru_') or x.replace("-ltoaru_","") != libname]
        print("base/lib/libtoaru_{lib}.so: {source} {headers} util/auto-dep.py | {libraryfiles} $(LC)\n\t$(CC) $(CFLAGS) {includes} -shared -fPIC -o $@ $< {libraries}".format(
            lib=libname,
            source=filename,
            headers=" ".join([toheader(x) for x in c.libs]),
            libraryfiles=" ".join([todep(x)[1] for x in _libs]),
            libraries=" ".join([x for x in _libs]),
            includes=" ".join([x for x in c.includes if x is not None])
            ))

