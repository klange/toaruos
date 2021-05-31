FROM ubuntu:20.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
	&& apt-get install -y build-essential python3 xorriso genext2fs mtools gnu-efi git automake autoconf wget libgmp-dev libmpfr-dev libmpc-dev flex bison texinfo dosfstools \
	&& rm -rf /var/lib/apt/lists/* /var/cache/apt/apt-file/*
