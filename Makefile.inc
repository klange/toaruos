CC = gcc
LD = ld -m elf_i386
CFLAGS = -Wall -Wextra -pedantic -m32 -O0 -std=c99 -finline-functions -fno-stack-protector -nostdinc -ffreestanding -Wno-unused-function -Wno-unused-parameter
NASM = nasm -f elf
ECHO = `which echo` -e
