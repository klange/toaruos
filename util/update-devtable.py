#!/usr/bin/env python3
import os
import glob
from pathlib import Path

with open('util/devtable','w') as devtable:

    # Set sudo apps to setuid, executable, no write
    devtable.write('/bin/gsudo f 4555 0 0 - - - - -\n')
    devtable.write('/bin/sudo f 4555 0 0 - - - - -\n')

    # Set master.passwd to not be visible except by root
    devtable.write('/etc/master.passwd f 600 0 0 - - - - -\n') # /etc/master.passwd should be restricted

    # Copy permissions and set ownership for user files
    for user_details in [('local',1000)]:
        user, uid = user_details
        for path in glob.glob('./base/home/{user}/**'.format(user=user),recursive=True):
            p = Path(path)
            path_mod = path.replace('./base','').rstrip('/')
            path_type = 'd' if p.is_dir() else 'f'
            st = os.stat(path)
            mode = '{:o}'.format(st.st_mode & 0o7777)
            devtable.write('{path_mod} {path_type} {mode} {uid} {uid} - - - - -\n'.format(path_mod=path_mod,path_type=path_type,mode=mode,uid=uid))

    # Special case /tmp to allow all users to write
    devtable.write('/tmp d 777 0 0 - - - - -\n')

