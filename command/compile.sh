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

# 写入没有文件系统的裸盘第300号逻辑扇区

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

BIN="prog_no_arg"
CFLAGS="-Wall -c -fno-builtin -W -Wstrict-prototypes \
      -Wmissing-prototypes -Wsystem-headers -m32 -fno-stack-protector -g "
LIB="../lib/"
OBJS="-m elf_i386 ../build/string.o ../build/syscall.o \
      ../build/stdio.o ../build/assert.o"
DD_IN=$BIN
DD_OUT="/home/work/my_workspace/bochs/hd60M.img" 

gcc $CFLAGS -I $LIB -o $BIN".o" $BIN".c"
ld -e main $BIN".o" $OBJS -o $BIN
SEC_CNT=$(ls -l $BIN|awk '{printf("%d", ($5+511)/512)}')

#if [[ -f $BIN ]];then
#   dd if=./$DD_IN of=$DD_OUT bs=512 \
#   count=$SEC_CNT seek=300 conv=notrunc
#fi

##########   以上核心就是下面这三条命令   ##########
#gcc -Wall -c -fno-builtin -W -Wstrict-prototypes -Wmissing-prototypes \
#   -Wsystem-headers -I ../lib -o prog_no_arg.o prog_no_arg.c
#ld -e main prog_no_arg.o ../build/string.o ../build/syscall.o\
#   ../build/stdio.o ../build/assert.o -o prog_no_arg
#dd if=prog_no_arg of=/home/work/my_workspace/bochs/hd60M.img \
#   bs=512 count=10 seek=300 conv=notrunc
