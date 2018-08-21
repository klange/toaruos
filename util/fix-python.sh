#!/bin/bash

# needed for strip
source util/activate.sh

HDD_PATH=base

pushd $HDD_PATH/usr/python/lib || exit 1

	echo "Stripping shared library..."
	chmod +w libpython3.6m.so
	i686-pc-toaru-strip libpython3.6m.so
	chmod -w libpython3.6m.so

	echo "Killing __pycache__ directories..."
	find . -name __pycache__ -exec rm -r "{}" \;

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

echo "Installing readline hook..."
cp util/readline._py $HDD_PATH/usr/python/lib/python3.6/
