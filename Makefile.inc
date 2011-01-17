CC = gcc
LD = ld -m elf_i386
CFLAGS = -Wall -m32 -O0 -fstrength-reduce -fomit-frame-pointer -finline-functions -nostdinc -fno-builtin
NASM = nasm -f elf

