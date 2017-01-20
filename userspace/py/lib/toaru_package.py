"""
Library for managing get-py packages.
"""
import json
import signal
import subprocess
import sys
import hashlib
import os
import stat

url = 'http://toaruos.org'

MANIFEST_PATH = '/tmp/.manifest.json'
INSTALLED_PATH = '/tmp/.installed.json'

installed_packages = []
packages_to_install = []
upgrade_packages = []
install_packages = []
dryrun = False
manifest = None
is_gui = False


def compare_version(left,right):
    if left[0] > right[0]: return True
    if left[0] == right[0] and left[1] > right[1]: return True
    if left[0] == right[0] and left[1] == right[1] and left[2] > right[2]: return True
    return False

def fetch_file(path, output, check=False, url=url, gui=False):
    loop = 0
    while loop < 3:
        if gui:
            args = ['fetch', '-m', '-o', output]
            args.append(f'{url}/{path}')
            fetch = subprocess.Popen(args, stdout=subprocess.PIPE)
            progress = subprocess.Popen(['progress-bar.py',f"Fetching {path}..."], stdin=fetch.stdout)
            fetch.stdout.close()
            progress.wait()
        else:
            args = ['fetch','-o',output]
            if check:
                args.append('-v')
            args.append(f'{url}/{path}')
            subprocess.call(args)
        if check:
            s = hashlib.sha512()
            with open(output,'rb') as f:
                for chunk in iter(lambda: f.read(4096), b""):
                    s.update(chunk)
            if s.hexdigest() != check:
                if loop == 2:
                    print("Too many bad checksums, bailing.")
                else:
                    print("Bad checksum, trying again...")
                    loop += 1
                    continue
        break

def process_package(name):
    if not name in manifest['packages']:
        raise ValueError((f"Unknown package: {name}"))
    package = manifest['packages'][name]
    if 'deps' in package:
        for dep in package['deps']:
            if dep not in packages_to_install:
                process_package(dep)
    packages_to_install.append(name)

def install_file(file,source):
    print(f"- Retrieving file {file[0]}","and checking hash" if file[2] else "")
    if not dryrun:
        fetch_file(file[0],file[1],file[2],source,is_gui)

def install_icon(steps):
    if not os.path.exists('/usr/share/icons/external'):
        subprocess.call(['mount','tmpfs','x','/usr/share/icons/external'])
    if not os.path.exists('/usr/share/menus'):
        subprocess.call(['mount','tmpfs','x','/usr/share/menus'])
    if not steps[2] in ['accessories','games','demo','graphics','settings']:
        return
    if not os.path.exists(f'/usr/share/menus/{steps[2]}'):
        os.mkdir(f'/usr/share/menus/{steps[2]}')
    fetch_file(steps[3],f'/usr/share/icons/external/{steps[4]}.png')
    with open(f'/usr/share/menus/{steps[2]}/{steps[4]}.desktop','w') as f:
        f.write(f"{steps[4]},{steps[5]},{steps[1]}\n")
    pid = None

    if os.path.exists('/tmp/.wallpaper.pid'):
        with open('/tmp/.wallpaper.pid','r') as f:
            pid = int(f.read().strip())
    if pid:
        os.kill(pid, signal.SIGUSR1)

def run_install_step(step):
    if step[0] == 'ln':
        print(f"- Linking {step[2]} -> {step[1]}")
        if not dryrun:
            os.symlink(step[1],step[2])
    elif step[0] == 'tmpfs':
        print(f"- Mounting tmpfs at {step[1]}")
        if not dryrun:
            subprocess.call(['mount','tmpfs','x',step[1]])
    elif step[0] == 'ext2':
        print(f"- Mounting ext2 image {step[1]} at {step[2]}")
        if not dryrun:
            subprocess.call(['mount','ext2',step[1] + ',nocache',step[2]])
    elif step[0] == 'chmodx':
        print(f"- Making {step[1]} executable")
        if not dryrun:
            current = os.stat(step[1]).st_mode
            os.chmod(step[1],current | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    elif step[0] == 'ungz':
        print(f"- Decompressing {step[1]}")
        if not dryrun:
            subprocess.call(['ungz',step[1]])
    elif step[0] == 'mkdir':
        print(f"- Creating directory {step[1]}")
        if not dryrun:
            if not os.path.exists(step[1]):
                os.mkdir(step[1])
    elif step[0] == 'menu':
        print(f"- Installing shortcut {step[1]} in {step[2]}")
        if not dryrun:
            install_icon(step)
    else:
        print("Unknown step:",step)

def install_package(name):
    if not name in manifest['packages']:
        print(f"Unknown package: {arg}")
        sys.exit(1)
    package = manifest['packages'][name]
    if 'pre_steps' in package:
        for step in package['pre_steps']:
            run_install_step(step)
    if 'files' in package:
        url_source = url
        if 'source' in package:
            source = package['source']
            if source not in manifest['sources']:
                print(f"Bad source '{source}', will try locally.")
            url_source = manifest['sources'][source]
        for file in package['files']:
            install_file(file,url_source)
    if 'post_steps' in package:
        for step in package['post_steps']:
            run_install_step(step)
    installed_packages[name] = package['version']

def calculate_upgrades():
    for name in packages_to_install:
        if name in installed_packages:
            if compare_version(manifest['packages'][name]['version'],installed_packages[name]):
                upgrade_packages.append(name)
        else:
            install_packages.append(name)

def write_status():
    if not dryrun:
        with open(INSTALLED_PATH,'w') as f:
            json.dump(installed_packages, f)

def fetch_manifest():
    global manifest
    global installed_packages
    fetch_file('manifest.json',MANIFEST_PATH)

    with open(MANIFEST_PATH) as f:
        manifest = json.load(f)

    try:
        with open(INSTALLED_PATH) as f:
            installed_packages = json.load(f)
    except:
        installed_packages = {}

    if not 'packages' in manifest:
        raise ValueError("invalid manifest file")

