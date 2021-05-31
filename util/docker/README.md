1. Build the base image from the Dockerfile.

```bash
docker build .
```

2. Run the docker image with `-it bash`.

3. Clone the repository over `https`:

```bash
cd
git clone --recurse-submodules https://github.com/toaruos/misaka
cd misaka
util/build-toolchain.sh
cd util/build/binutils
while [[ -e confdir3/confdir3 ]]; do mv confdir3/confdir3 confdir3a; rmdir confdir3; mv confdir3a confdir3; done; rmdir confdir3
cd ../../..
mv local /root/gcc_local
cd /root
rm -rf misaka
```

