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

    # Skip the header directory that is already installed to /usr/include
    if tarinfo.name.startswith('src/kuroko/kuroko'):
        return None

    return tarinfo

def symlink(file,target):
    ti = tarfile.TarInfo(file)
    ti.type = tarfile.SYMTYPE
    ti.linkname = target
    return ti

with tarfile.open('ramdisk.igz','w:gz') as ramdisk:
    # Core ramdisk
    ramdisk.add('base',arcname='/',filter=file_filter)

    # Symlinks that are part of normal system distribution
    ramdisk.addfile(symlink('bin/sh','esh'))
    ramdisk.addfile(symlink('bin/mandelbrot','julia'))
    ramdisk.addfile(symlink('bin/fgrep','grep'))

    # Build tools; you'll probably want these.
    ramdisk.add('util/auto-dep.krk',arcname='/bin/auto-dep.krk',filter=file_filter)
    ramdisk.add('kuroko/src/kuroko',arcname='/usr/include/kuroko',filter=file_filter)

    if os.getenv('NO_SRC_DIR') != '1':
        # Everything else is the /src directory, which is optional.
        for d in ('/src','/src/bim'):
            ramdisk.add('.',arcname=d,filter=file_filter,recursive=False)

        for s in ('apps','kernel','linker','lib','libc','boot','modules','tests','tags','bim/bim.c','bim/bim.h'):
            if os.path.exists(s): # mostly to skip 'tags' if it's not been built
                ramdisk.add(s,arcname=f'/src/{s}',filter=file_filter)

        # Kuroko sources are around ~1M, so disable them if that's too big for you.
        ramdisk.add('kuroko/src','/src/kuroko',filter=file_filter)
