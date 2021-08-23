# x86_os

learn 《真象还原》

&nbsp;

逻辑扇区号：mbr=0, loader=2, kenerl=9

&nbsp;

### 编译选项

gcc -m32 使用64位编译器生成32位elf

ld的时候加上 -m elf_i386

&nbsp;

-fno-builtin 不使用C库函数

也可以-fno-builtin-function后边跟某一单个不使用的C库函数

&nbsp;

-fno-stack-protector

编译源码到目标文件时，一定要加“-fno-stack-protector”，不然默认会调函数“__stack_chk_fail”进行栈相关检查，然而是手动裸ld去链接，没有链接到“__stack_chk_fail”所在库文件，所以在链接过程一定会报错: undefined reference to `__stack_chk_fail'。解决办法不是在链接过程中，而是在编译时加此参数，强制gcc不进行栈检查，从而解决。

此外，ld 的时候加上参数"-e main"就可以了，意思是将main函数作为程序入口，ld 的默认程序入口为_start。

&nbsp;

-w的意思是关闭编译时的警告，也就是编译后不显示任何warning，因为有时在编译之后编译器会显示一些例如数据转换之类的警告，这些警告是我们平时可以忽略的。
-Wall选项意思是编译后显示所有警告。

-W选项类似-Wall，会显示警告，但是只显示编译器认为会出现错误的警告。

在编译一些项目的时候可以-W和-Wall选项一起使用。

&nbsp;

-Wstrict-prototypes 函数声明中必须有参数类型。

&nbsp;

-Wmissing-prototypes 选项要求函数必须有声明，否则编译时发出警告。 

&nbsp;

-Wsystem-headers

Print warning messages for constructs found in system header files. **Warnings from system headers are normally suppressed**, on the assumption that they usually do not indicate real problems and would only make the compiler output harder to read.

说实话，还是没明白这个。

&nbsp;

&nbsp;

### 物理内存使用

loader 0x900

MBR 0x7c00

内核 0x7_0000

显存 0xB_8000

页表 0x10_0000

![页表](https://github.com/WM-CH/x86_os/raw/master/%E9%A1%B5%E8%A1%A8.png)



&nbsp;

&nbsp;

### 虚拟内存布局

|

|

+---------0xC009A000---------位图地址

|

+0xC009B000

| ··· ··· 4个页大小的位图，管理512M内存

+0xC009C000

| ··· ··· 512M是本系统最大的内存空间

+0xC009D000

|

+---------0xC009E000---------内核主线程PCB（往下使用）

|

+---------0xC009F000---------内核主线程栈顶（先自减/再入栈）

|

| ··· ··· 装载内核，约70k=0x11800

|

+---------0xC0100000---------K_HEAP_START 跨过1M，使虚拟内存地址连续

|

|



### 线程切换

1.理解thread.c中 thread_start 中 ret，它实现的线程切换。

thread_start 给普通线程，分配一个页，作为PCB。然后调用 init_thread 和 thread_crete。

- init_thread 给普通线程，填充PCB，其中将 self_kstack 赋值为PCB最顶端！
- thread_create 给普通线程，预留出"中断栈intr_stack"空间，预留出"线程栈thread_stack"并赋值。注意 self_kstack 的位置：

; PCB最顶端

; intr_stack ~

; thread_stack <---- self_kstack

thread_stack内容如下：

; fun_arg

; function

; unused_retaddr

; eip

; esi

; edi

; ebx

; ebp 		<---- self_kstack 赋值给 esp

本次演示 thread_start 中 调用内联汇编进行任务切换。

ret 之前，使 thread->self_kstack 的值作为栈顶，获取了栈顶指针，pop出去4个寄存器，此时 esp指向了"线程栈thread_stack"成员eip

ret使当前esp赋值给eip，调用到 kernel_thread 函数

在 kernel_thread 函数中时栈的内容是：

; fun_arg

; function

; unused_retaddr		<--- esp

问：这里一直用的是0特权级的栈？

答：是的，因为  thread_start 是主线程调用的。



&nbsp;

2.理解switch.c中 switch_to 实现的线程切换。

make_main_thread 不再需要分配内存给PCB，因为预留了 0xC009E000 作为PCB

栈顶是 0xC009F000

只是调用 init_thread 初始化PCB，其中将 self_kstack 赋值为PCB最顶端，也就是栈顶地址 0xC009F000

PCB结构从 0xC009E000 开始，最后一个成员是magic_num，

栈顶从 0xC009F000 开始往低地址处压栈，检测到magic_num时，说明破坏了PCB结构。

&nbsp;

- 主线程执行时，发生时钟中断并切换线程。

kernel.s中的 intr%1entry 保护现场，栈中保存一个"中断栈" intr_stack的内容，

调用时钟中断函数 intr_timer_handler

调用thread.c中schedule的调度

调用switch.s中的switch_to

switch_to(cur, next); 两个参数是两个线程的PCB

调用switch_to时"主线程"的栈：

; 前边还有很多压栈的数据，包括主线程中断之前压栈的一些返回地址，中断栈，以及intr_timer_handler、schedule和switch_to的返回地址。

; next <---- esp+24

; cur <---- esp+20

; 返回地址 <---- esp+16

在switch_to中根据ABI规定，压入"cur线程"的4个寄存器

; esi <---- esp+12

; edi <---- esp+8

; ebx <---- esp+4

; ebp <---- esp 赋值给 self_kstack

最后将栈顶指针esp赋值给 => PCB的第一个成员"线程内核栈栈顶指针"self_kstack

下边恢复普通线程，

假设它是第一次被调度执行。

从next的PCB获取到 self_kstack ，将它内容作为栈顶esp【这时 self_kstack 的位置见1里面】

之后 pop 4个寄存器、调用ret将esp赋值给eip。就是调用到 kernel_thread 函数。

此时主线程的中断流程并没有走完，没有退回到schedule、intr_timer_handler，也没有执行 intr_exit！！！

&nbsp;

问：中断栈的位置？

答：初始情况下此栈在线程自己的内核栈中位置固定，在 PCB 所在页的最顶端，

每次进入中断时就不一定了，如果进入中断时不涉及到特权级变化，它的位置就会在当前的 esp 之下，

否则处理器会从 TSS 中获得新的 esp 的值，然后该栈在新的 esp 之下

&nbsp;

- 普通线程执行时，发生时钟中断并切换线程，假设切换回主线程。

kernel.s中的 intr%1entry 保护现场，栈中保存一个"中断栈" intr_stack的内容，

调用时钟中断函数 intr_timer_handler

调用thread.c中schedule的调度

调用switch.s中的switch_to

switch_to(cur, next); 两个参数是两个线程的PCB

调用switch_to时"线程"的栈：

; 前边还有很多压栈的数据，包括一个中断栈，以及intr_timer_handler、schedule和switch_to的返回地址。

; next <---- esp+24

; cur <---- esp+20

; 返回地址 <---- esp+16

在switch_to中根据ABI规定，压入"cur线程"的4个寄存器

; esi <---- esp+12

; edi <---- esp+8

; ebx <---- esp+4

; ebp <---- esp 赋值给 self_kstack

最后将栈顶指针esp赋值给 => PCB的第一个成员"线程内核栈栈顶指针"self_kstack

下边恢复主线程，

从next的PCB获取到 self_kstack ，将它内容作为栈顶esp

之后 pop 4个寄存器、调用ret将esp赋值给eip。这次是返回到了switch_to的调用者 schedule 

然后退回到 intr_timer_handler，intr_exit

接着被中断前的位置继续执行了。。。

