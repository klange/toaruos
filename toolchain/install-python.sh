#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd $DIR/..

HDD_PATH=`pwd`/hdd

which python3.6 > /dev/null || echo "You need Python 3.6 to cross-compile Python 3.6. You should build it locally or obtain it from your package manager if possible. A PPA for Ubuntu is available at ppa:jonathonf/python-3.6" && exit 1

if [ ! -d toaru-python ]; then
	echo "No Python source checkout, cloning..."
	git clone https://github.com/klange/cpython toaru-python
fi

echo "Installing dlfcn.h..."
mkdir -p hdd/usr/include
cp userspace/lib/dlfcn.h hdd/usr/include/

pushd toaru-python || exit 1
	echo "Configuring..."

	./configure --disable-ipv6 --enable-shared --host=i686-pc-toaru --build=i686 --prefix=/usr/python ac_cv_file__dev_ptmx=no ac_cv_file__dev_ptc=no ac_cv_func_dlopen=yes ac_cv_func_wait3=no ac_cv_var_tzname=no ac_cv_func_unsetenv=no ac_cv_var_putenv=no ac_cv_header_sys_lock_h=no ac_cv_header_sys_param_h=no ac_cv_header_sys_resource_h=no ac_cv_header_libintl_h=no ac_cv_func_sigaction=no

	echo "Making..."
	make

	echo "Installing..."
	make DESTDIR=$HDD_PATH commoninstall bininstall || exit 1

popd

pushd $HDD_PATH/usr/python/lib || exit 1

	echo "Stripping shared library..."
	chmod +w libpython3.6m.so
	i686-pc-toaru-strip libpython3.6m.so
	chmod -w libpython3.6m.so

	echo "Killing __pycache__ directories..."
	rm -r ./python3.6/__pycache__
	rm -r ./python3.6/importlib/__pycache__

	# Let's kill some other shit while we're in here
	pushd python3.6 || exit 1
		echo "Cleaning up unused modules..."
		rm -r test distutils tkinter multiprocessing ensurepip config-3.6m/libpython3.6m.a
	popd

popd

pushd $HDD_PATH/usr
	if [ ! -d bin ]; then
		mkdir bin
	fi

	pushd bin

		# Can never be too careful.
		ln -s ../python/bin/python3.6 python3.6
		ln -s ../python/bin/python3.6 python3
		ln -s ../python/bin/python3.6 python

	popd

	pushd lib

		ln -s ../python/lib/libpython3.6m.so

	popd
popd
