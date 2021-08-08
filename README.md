# x86_os

learn 《真象还原》



kernel和lib/kernel目录的文件，需要在ubuntu上编译，生成elf文件。

build_kernel.sh需要64位的gcc生成32位的可执行程序。

build_kernel.sh同时编译lib/kernel和kernel目录的文件，生成kenerl.bin。

逻辑扇区号：mbr=0, loader=2, kenerl=9