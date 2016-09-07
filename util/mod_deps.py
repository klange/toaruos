#!/usr/bin/env python2
# coding: utf-8
"""
Extract dependencies from kernel modules.
"""

import os
import sys
import subprocess

def processModule(path):
    p = subprocess.Popen(["i686-pc-toaru-nm",path,"-p"], stdout=subprocess.PIPE)
    symbols, errors = p.communicate()
    name = [ x[2].replace("module_info_","") for x in [x.strip().split(" ") for x in symbols.split("\n") if len(x.strip().split(" ")) > 2] if x[1] == "D" and x[2].startswith("module_info_") ][0]
    dependencies = [ x[2].replace("_mod_dependency_","") for x in [x.strip().split(" ") for x in symbols.split("\n") if len(x.strip().split(" ")) > 2] if x[1] == "d" and x[2].startswith("_mod_dependency_") ]
    return path, name, dependencies

modules = {}
files = {}

# Read the symbols from the file.
for module in os.listdir("hdd/mod"):
    if module.endswith(".ko"):
        path,name,deps = processModule("hdd/mod/" + module)
        modules[name] = (path,deps)
        files[path] = name

# Okay, now let's spit out the dependencies for our module.
if len(sys.argv) < 2:
    print >>sys.stderr, "Expected a path to a module (from the root of the build tree), eg. hdd/mod/test.ko"
    sys.exit(1)

me = sys.argv[-1]
name = files[me]
deps = modules[name][1]

def calculate(depends, new):
    for k in new:
        if not k in depends:
            depends.append(k)
            _, other = modules[k]
            depends = calculate(depends, other)
    return depends

depends = calculate([], deps)

satisfied = []
a = depends[:]

while set(satisfied) != set(depends):
    b = []
    for k in a:
        if all([x in satisfied for x in modules[k][1]]):
            satisfied.append(k)
        else:
            b.append(k)
    a = b[:]

if len(sys.argv) > 2:
    for i in sys.argv[1:-1]:
        if i == '--print-deps':
            print name, "→", " ".join(["\033[31m{mod}\033[m".format(mod=x) if not x in deps else x for x in satisfied])

        if i == '--print-files':
            for i in satisfied:
                print modules[i][0], "(dep-of-dep)" if i not in deps else ""


p = subprocess.Popen(["i686-pc-toaru-ld","-T","kernel/link.ld","-o","/dev/null","toaruos-kernel",]+[modules[x][0] for x in satisfied]+[me])
sys.exit(p.wait())


