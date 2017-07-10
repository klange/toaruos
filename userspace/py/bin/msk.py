#!/usr/bin/python3.6
"""
Misaka Package Manager

Fetches and installs packages in compressed tarballs.
"""

import glob
import hashlib
import json
import os
import signal
import subprocess
import sys
import tarfile

server_url = 'http://toaruos.org/packages'
var_dir = '/var/msk'
manifest_path = f'{var_dir}/manifest.json'
manifest_url = f'{server_url}/manifest.json'
index_path = f'{var_dir}/index.json'
local_cache = f'{var_dir}/cache'

is_gui = False

def fetch(url, destination, check=False):
    """Fetch a package or other file from `url` and write it to `destination`."""
    if is_gui:
        args = ['fetch','-m','-o',destination,url]
        _fetch = subprocess.Popen(args, stdout=subprocess.PIPE)
        _progress = subprocess.Popen(['progress-bar.py',f'Fetching {os.path.basename(url)}...'], stdin=_fetch.stdout)
        _fetch.stdout.close()
        _progress.wait()
    else:
        args = ['fetch','-v','-o',destination,url]
        subprocess.call(args)
    if check:
        sha = hashlib.sha512()
        with open(destination,'rb') as f:
            for chunk in iter(lambda: f.read(4096), b""):
                sha.update(chunk)
        if sha.hexdigest() != check:
            return False
    return True

def try_fetch(url, destination):
    """Make multiple attempts to download a file with an integrity checksum."""
    attempts = 0
    while attempts < 3:
        if fetch(url, destination, check=True):
            return True
        else:
            attempts += 1
    return False

def extract_package(path, quiet=True):
    with tarfile.open(path, 'r:gz') as package:
        try:
            manifest_inf = package.getmember('manifest.json')
            manifest_br  = package.extractfile(manifest_inf)
            manifest_str = manifest_br.read()
        except KeyError:
            if not quiet:
                print("Invalid MSK:", path)
            return False

        manifest = json.loads(manifest_str)
        if not quiet:
            print(manifest['package'])
            print('.'.join([str(x) for x in manifest['version']]))

            print("contents:")
            for member in package.getnames():
                if not member.startswith('./'):
                    continue
                print(member.replace('./','/',1))

            print("Going to extract to /")

        members = [member for member in package.getmembers() if member.name.startswith('./')]
        package.extractall('/',members=members)

def signal_desktop(pid_file='/tmp/.wallpaper.pid'):
    if os.path.exists(pid_file):
        with open(pid_file,'r') as f:
            pid = int(f.read().strip())
    if pid:
        os.kill(pid, signal.SIGUSR1)

def version_str(version_lst):
    return '.'.join([str(x) for x in version_lst])

def needs_var_dir():
    if not os.path.exists(var_dir):
        os.makedirs(var_dir)

def needs_local_cache():
    needs_var_dir()
    if not os.path.exists(local_cache):
        os.makedirs(local_cache)

def fetch_manifest():
    needs_var_dir()
    fetch(manifest_url, manifest_path)

def get_manifest():
    if not os.path.exists(manifest_path):
        fetch_manifest()
    with open(manifest_path, 'r') as f:
        return json.loads(f.read())

def get_local_index():
    if not os.path.exists(index_path):
        return {}
    else:
        with open(index_path, 'r') as f:
            return json.loads(f.read())

def commit_local_index(index):
    needs_var_dir()
    with open(index_path,'w') as f:
        f.write(json.dumps(index))

def resolve_dependencies(packages, local_index, manifest, output=None):
    if not output:
        output = []
    for package in packages:
        for dep in manifest[package]['depends']:
            if not dep in packages and dep not in output and dep not in local_index:
                output = resolve_dependencies([dep], local_index, manifest, output)
        output.append(package)
    return output

def show_usage():
    print(f"""msk - Download and install packages.

usage: {sys.argv[0]} install PACKAGE [PACKAGE ...]
       {sys.argv[0]} update
       {sys.argv[0]} remove PACKAGE [PACKAGE ...] (* unimplemented)
       {sys.argv[0]} list
       {sys.argv[0]} list-all
       {sys.argv[0]} help
""")
    return 1

def update_manifest():
    fetch_manifest()
    return 0

def remove_packages():
    print("(Unimplemented)")
    return 1

def fetch_package(name, manifest):
    package = manifest[name]
    fetch(f"{server_url}/{package['file']}", f"{local_cache}/{package['file']}", check=package['checksum'])

def install_fetched_package(name, manifest, local_index, install_candidates):
    package = manifest[name]

    # Extract tarball
    extract_package(f"{local_cache}/{package['file']}")

    # Remove the extracted package
    os.remove(f"{local_cache}/{package['file']}")

    # Update the index to mark the package as installed
    if not name in local_index:
        local_index[name] = {}
    local_index[name].update(manifest[name])
    if name in install_candidates:
        local_index[name]['status'] = 'I' # Installed directly.
    else:
        if 'status' not in local_index[name]:
            local_index[name]['status'] = 'i' # Installed as a dependency

def install_packages():
    needs_local_cache()
    local_index = get_local_index()
    if local_index is None:
        print("Failed to build a local index.")
        return 1
    manifest = get_manifest()
    if manifest is None:
        print("Failed to obtain manifest.")
        return 1

    # Verify the requested package names are valid
    packages = sys.argv[2:]
    install_candidates = []
    for package in packages:
        if package in local_index:
            continue
        if package not in manifest:
            print("Package not found:", package)
            continue
        install_candidates.append(package)

    # Nothing was valid or the input was empty...
    if not install_candidates:
        print("Nothing to install.")
        return 1

    # Go through each package and calculate dependency tree.
    all_packages = resolve_dependencies(install_candidates, local_index, manifest)

    # If the set of packages we are installing differs from what
    # was requested (and valid), warn the user before continuing
    # (this just means there were dependencies to also install)
    if set(all_packages) != set(install_candidates):
        print("Going to install:", ", ".join(all_packages))
        if input("Continue? [y/N] ") not in ['y','Y','yes','YES']:
            print("Stopping.")
            return 1

    # Download all of the requested packages
    for name in all_packages:
        print(f"Downloading {name}...")
        fetch_package(name, manifest)

    # Install the packages
    for name in all_packages:
        print(f"Installing {package['file']}...")
        install_fetched_package(name, manifest, local_index, install_candidates)

    # Commit
    commit_local_index(local_index)

    # Signal desktop for menu changes
    signal_desktop()

    return 0

def list_installed_packages():
    local_index = get_local_index()
    for name, package in local_index.items():
        print(f"{name} {version_str(package['version'])} - {package['friendly-name']}")
    return 0

def list_all_packages():
    local_index = get_local_index()
    manifest = get_manifest()
    for name, package in manifest.items():
        info = " " if name not in local_index else local_index[name]['status']
        print(f"{info} {name} {version_str(package['version'])} - {package['friendly-name']}")
    return 0

__commands = {
    'update': update_manifest,
    'help': show_usage,
    'install': install_packages,
    'remove': remove_packages,
    'list': list_installed_packages,
    'list-all': list_all_packages,
}

if __name__ == '__main__':
    if len(sys.argv) < 2:
        sys.exit(show_usage())

    if sys.argv[1] in __commands:
        sys.exit(__commands[sys.argv[1]]())
    else:
        print("Unrecognized command:", sys.argv[1])
        sys.exit(1)

