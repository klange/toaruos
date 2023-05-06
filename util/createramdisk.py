#!/usr/bin/python3
"""
Generates, from this source repository, a "tarramdisk" - a ustar archive
suitable for booting ToaruOS. 
"""

import os
import tarfile

users = {
    'root': 0,
    'local': 1000,
    'guest': 1001,
}

restricted_files = {
    'etc/master.passwd': 0o600,
    'etc/sudoers': 0o600,
    'tmp': 0o777,
    'var': 0o755,
    'bin/sudo': 0o4555,
    'bin/gsudo': 0o4555,
}

def file_filter(tarinfo):
    # Root owns files by default.
    tarinfo.uid = 0
    tarinfo.gid = 0

    if tarinfo.name.startswith('home/'):
        # Home directory contents are owned by their users.
        user = tarinfo.name.split('/')[1]
        tarinfo.uid = users.get(user,0)
        tarinfo.gid = tarinfo.uid
    elif tarinfo.name in restricted_files:
        tarinfo.mode = restricted_files[tarinfo.name]

    if tarinfo.name.startswith('usr/include/kuroko') and tarinfo.type == tarfile.SYMTYPE:
        return None

    if tarinfo.name.startswith('src'):
        # Let local own the files here
        tarinfo.uid = users.get('local')
        tarinfo.gid = tarinfo.uid
        # Skip object files
        if tarinfo.name.endswith('.so') or tarinfo.name.endswith('.o') or tarinfo.name.endswith('.sys'):
            return None

    return tarinfo

def symlink(file,target):
    ti = tarfile.TarInfo(file)
    ti.type = tarfile.SYMTYPE
    ti.linkname = target
    return ti

with tarfile.open('ramdisk.igz','w:gz') as ramdisk:
    ramdisk.add('base',arcname='/',filter=file_filter)

    ramdisk.add('.',arcname='/src',filter=file_filter,recursive=False) # Add a src directory
    ramdisk.add('apps',arcname='/src/apps',filter=file_filter)
    ramdisk.add('kernel',arcname='/src/kernel',filter=file_filter)
    ramdisk.add('linker',arcname='/src/linker',filter=file_filter)
    ramdisk.add('lib',arcname='/src/lib',filter=file_filter)
    ramdisk.add('libc',arcname='/src/libc',filter=file_filter)
    ramdisk.add('boot',arcname='/src/boot',filter=file_filter)
    ramdisk.add('modules',arcname='/src/modules',filter=file_filter)
    if os.path.exists('tags'):
        ramdisk.add('tags',arcname='/src/tags',filter=file_filter)
    ramdisk.add('util/auto-dep.krk',arcname='/bin/auto-dep.krk',filter=file_filter)
    ramdisk.add('kuroko/src/kuroko',arcname='/usr/include/kuroko',filter=file_filter)
    ramdisk.addfile(symlink('bin/sh','esh'))
    ramdisk.addfile(symlink('bin/mandelbrot','julia'))


