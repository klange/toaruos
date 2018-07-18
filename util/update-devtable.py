#!/usr/bin/env python3
import os
import glob
from pathlib import Path

with open('util/devtable','w') as devtable:
    devtable.write('/bin/sudo f 4555 0 0 - - - - -\n') # sudo always needs setuid
    devtable.write('/etc/master.passwd f 600 0 0 - - - - -\n') # /etc/master.passwd should be restricted

    # Now add user home directories
    for user_details in [('local',1000)]:
        user, uid = user_details
        for path in glob.glob(f'./base/home/{user}/**',recursive=True):
            p = Path(path)
            path_mod = path.replace('./base','')
            path_type = 'd' if p.is_dir() else 'f'
            st = os.stat(path)
            mode = '{:o}'.format(st.st_mode & 0o7777)
            devtable.write(f'{path_mod} {path_type} {mode} {uid} {uid} - - - - -\n')

