####  此脚本应该在command目录下执行
# 程序 prog_no_arg.c 中用到了函数 printf，
# 头文件 stdio.h 要指定路径 -I ../lib
# 在链接的时候除了要加上 stdio.o 外， 还要加上 stdio.h（也是 stdio.o）所依赖的目标文件，
# 包括 string.o、 syscall.o 和 assert.o。
# 这些目标文件都是 build 目录下的，因此一定要先编译内核
# 但这并不是说，用户程序的库文件依赖于内核的目标文件，
# 并不是用了内核的目标文件就是执行了内核的代码，这仅仅表示用户进程中执行的代码和内核目标文件中的代码是一样的，
# 在内存中它们是独立的两份拷贝，互不干涉。
# 无论是用谁的目标文件都不重要，目标（库）文件只是系统调用的封装而已，
# 不同的库文件最终的出路都是相同的，都是通过系统调用发送 0x80 号中断，利用中断门连接到唯一的内核。

# 目标文件的链接顺序，本着 “调用在前，定义在后”

# prog_no_arg	写入没有文件系统的裸盘第300号逻辑扇区
# prog_arg		写入没有文件系统的裸盘第400号逻辑扇区
# cat			写入没有文件系统的裸盘第500号逻辑扇区

if [[ ! -d "../lib" || ! -d "../build" ]];then
   echo "dependent dir don\`t exist!"
   cwd=$(pwd)
   cwd=${cwd##*/}
   cwd=${cwd%/}
   if [[ $cwd != "command" ]];then
      echo -e "you\`d better in command dir\n"
   fi 
   exit
fi

BIN1="prog_no_arg"
BIN2="prog_arg"
BIN3="cat"
CFLAGS="-Wall -c -fno-builtin -W -Wstrict-prototypes \
      -Wmissing-prototypes -Wsystem-headers -m32 -fno-stack-protector -g \
	  -I ../lib/ -I ../lib/kernel/ -I ../lib/user/ -I \
      ../kernel/ -I ../device/ -I ../thread/ -I \
      ../userprog/ -I ../fs/ -I ../shell/ "
OBJS="../build/string.o ../build/syscall.o \
      ../build/stdio.o ../build/assert.o start.o"
#DD_IN=$BIN
#DD_OUT="/home/work/my_workspace/bochs/hd60M.img" 

# 1 -v 打印详细信息
gcc -v -nostdinc -nostdlib $CFLAGS -o $BIN1".o" $BIN1".c"
ld -v  -nostdinc -nostdlib -m elf_i386 -e main $BIN1".o" $OBJS -o $BIN1
echo "-------------------------------------------------------"

# 2
nasm -f elf ./start.S -o ./start.o
ar rcs simple_crt.a $OBJS start.o
# ar 命令，将 string.o、 syscall.o、 stdio.o、 assert.o 和 start.o 打包成静态库文件 simple_crt.a
# simple_crt.a 类似于 CRT 的作用，它就是我们所说的简陋版 C 运行库。
# 后面的用户程序目标文件 prog_arg.o 和它直接链接就可以了。 
gcc -v -nostdinc -nostdlib $CFLAGS -o $BIN2".o" $BIN2".c"
ld -v  -nostdinc -nostdlib -m elf_i386 $BIN2".o" simple_crt.a -o $BIN2

# 3
#nasm -f elf ./start.S -o ./start.o
#ar rcs simple_crt.a $OBJS start.o
gcc -v -nostdinc -nostdlib $CFLAGS -o $BIN3".o" $BIN3".c"
ld -v  -nostdinc -nostdlib -m elf_i386 $BIN3".o" simple_crt.a -o $BIN3


#SEC_CNT=$(ls -l $BIN|awk '{printf("%d", ($5+511)/512)}')
#if [[ -f $BIN ]];then
#   dd if=./$DD_IN of=$DD_OUT bs=512 \
#   count=$SEC_CNT seek=300 conv=notrunc
#fi

##########   1. 以上核心就是下面这三条命令   ##########
#gcc -Wall -c -fno-builtin -W -Wstrict-prototypes -Wmissing-prototypes \
#   -Wsystem-headers -I ../lib -o prog_no_arg.o prog_no_arg.c
#ld -e main prog_no_arg.o ../build/string.o ../build/syscall.o\
#   ../build/stdio.o ../build/assert.o -o prog_no_arg
#dd if=prog_no_arg of=/home/work/my_workspace/bochs/hd60M.img \
#   bs=512 count=10 seek=300 conv=notrunc

##########   2. 以上核心就是下面这五条命令   ##########
#nasm -f elf ./start.S -o ./start.o
#ar rcs simple_crt.a ../build/string.o ../build/syscall.o \
#   ../build/stdio.o ../build/assert.o ./start.o
#gcc -Wall -c -fno-builtin -W -Wstrict-prototypes -Wmissing-prototypes \
#   -Wsystem-headers -I ../lib/ -I ../lib/user -I ../fs prog_arg.c -o prog_arg.o
#ld prog_arg.o simple_crt.a -o prog_arg
#dd if=prog_arg of=/home/work/my_workspace/bochs/hd60M.img \
#   bs=512 count=11 seek=300 conv=notrunc
