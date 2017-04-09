#!/usr/bin/python
"""
tree.py - List directories in a visual tree.
"""
import os

def print_directory(path, prefix="", last=False):
    if last:
        extra = "└─"
    else:
        extra = "├─"
    print(f"{prefix}{extra}{os.path.basename(path)}")
    if not last:
        prefix += "│ "
    else:
        prefix += "  "
    if os.path.isdir(path):
        subs = os.listdir(path)
        for i in range(len(subs)):
            p = os.path.join(path,subs[i])
            if i == len(subs)-1:
                print_directory(p,f"{prefix}",True)
            else:
                print_directory(p,f"{prefix}",False)

print_directory(".",last=True)
