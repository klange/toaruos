#!/usr/bin/env python3
# coding: utf-8
"""
Extract dependencies from kernel modules.
"""

import os
import sys
import subprocess

if os.uname().sysname == "toaru":
    prefix = ""
    link_ld = '/tmp/link.ld'
    mod_dir = '/mod'
    o_file = '/tmp/test'
else:
    prefix = "i686-elf-"
    link_ld = 'kernel/link.ld'
    mod_dir = 'hdd/mod'
    o_file = '/dev/null'


def processModule(path):
    p = subprocess.Popen([prefix+"nm",path,"-p"], stdout=subprocess.PIPE,bufsize=1)
    symbols, _ = p.communicate()
    symbols = symbols.decode('utf-8')
    name = [ x[2].replace("module_info_","") for x in [x.strip().split(" ") for x in symbols.split("\n") if len(x.strip().split(" ")) > 2] if x[1] == "D" and x[2].startswith("module_info_") ][0]
    dependencies = [ x[2].replace("_mod_dependency_","") for x in [x.strip().split(" ") for x in symbols.split("\n") if len(x.strip().split(" ")) > 2] if x[1] == "d" and x[2].startswith("_mod_dependency_") ]
    return path, name, dependencies

modules = {}
files = {}

# Read the symbols from the file.
for module in os.listdir(mod_dir):
    if module.endswith(".ko"):
        path,name,deps = processModule(mod_dir + "/" + module)
        modules[name] = (path,deps)
        files[path] = name

# Okay, now let's spit out the dependencies for our module.
if len(sys.argv) < 2:
    print("Expected a path to a module (from the root of the build tree), eg. %s/test.ko" % mod_dir, file=sys.stderr)
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
            print(name, "→", " ".join(["\033[31m{mod}\033[m".format(mod=x) if not x in deps else x for x in satisfied]))

        if i == '--print-files':
            for i in satisfied:
                print(modules[i][0], "(dep-of-dep)" if i not in deps else "")


if os.path.exists(link_ld) and os.path.exists('toaruos-kernel'):
    p = subprocess.Popen([prefix+"ld","-T",link_ld,"-o",o_file,"toaruos-kernel",]+[modules[x][0] for x in satisfied]+[me])
    sys.exit(p.wait())


