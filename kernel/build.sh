#!/bin/bash
gcc -m32 -c -o main.o main.c
ld -m elf_i386 main.o -Ttext 0xc0001500 -e main -o kernel.bin
