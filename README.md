# x86_os

learn 《真象还原》

&nbsp;

逻辑扇区号：mbr=0, loader=2, kenerl=9

&nbsp;

## 编译选项

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

## 反汇编

### 反汇编main.c

gcc -S -o main.s main.c

### 目标文件反汇编

gcc -c -o main.o main.c

objdump -s -d main.o > main.o.txt

反汇编同时显示源代码

gcc -g -c -o main.o main.c

objdump -S -d main.o > main.o.txt

显示源代码同时显示行号

objdump -j .text -ld -C -S main.o > main.o.txt

### 可执行文件反汇编

gcc -o main main.c

objdump -s -d main > main.txt

反汇编同时显示源代码

gcc -g -o main main.c

objdump -Sld kernel.bin > kernel.dis

### 常用参数

objdump -d <file(s)>: 将代码段反汇编；

objdump -S <file(s)>: 将代码段反汇编的同时，将反汇编代码与源代码交替显示，编译时需要使用-g参数，即需要调试信息；

objdump -C <file(s)>: 将C++符号名逆向解析

objdump -l <file(s)>: 反汇编代码中插入文件名和行号

objdump -j section <file(s)>: 仅反汇编指定的section

&nbsp;

## 物理内存使用

loader 0x900

MBR 0x7c00

内核 0x7_0000

显存 0xB_8000

页表 0x10_0000

![页表](https://github.com/WM-CH/x86_os/raw/master/%E9%A1%B5%E8%A1%A8.png)



&nbsp;

&nbsp;

## 虚拟内存布局

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



## 线程切换

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

所有的线程都是用的0特权级栈，因为都属于主进程！

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

&nbsp;

## 信号量、锁

假设只有两个线程。

主线程获取了锁，正在打印。

调度到另一个线程，（调度的时候主线程会保存一个中断栈，中断栈里面eflags的IF是打开中断的）

另一个线程获取锁，获取信号量时 thread_block 里面 intr_disable 关闭了调度，

主动schedule 切换到主线程

恢复主线程时，他的现场中eflags的IF是打开的。所以可以继续打开时钟中断进行调度。

主线程继续打印，

console_acquire();

put_str(str); 

console_release();

&nbsp;

## 用户进程

回顾之前线程的流程：

thread_start(...,function,...)

thread = get_kernel_pages(1)

init_thread(thread,...)

thread_create(thread,function,...)

kernel_thread(function, args)

function

把 function 替换为创建进程的新函数。

&nbsp;

### 自问自答

1.self_kstack的位置

答：是进程初始化之后，self_kstack的位置没动，见线程一节的描述。

2.用户进程3特权级栈在 **USER_STACK3_VADDR=(0xc0000000 - 0x1000)**

用户进程0特权级栈在 **PCB + PG_SIZE**

3.update_tss_esp只用了一个全局变量g_tss，怎么区分内核进程和用户进程的？

答：update_tss_esp函数只会被用户进程调用。

&nbsp;

readelf 选项

选项 -e,headers 显示全部头信息，等价于: -h -l -S 。

其中数据段的fileSize和memSize不相等，因为有BSS段。

&nbsp;

### 进程创建/切换流程

给用户进程创建内存位图，使用地址是：linux用户程序入口地址 0x80480000

给用户进程创建内存位图，使用地址是：linux用户程序入口地址 0x80480000

```c
/* 用户进程创建过程 */
void process_execute(void* filename, char* name) {
   struct task_struct* thread = get_kernel_pages(1);
   init_thread(thread, name, default_prio);			//初始化线程PCB结构体 struct task_struct
   create_user_vaddr_bitmap(thread);				//【进程新增】创建用户进程，虚拟地址空间位图
   thread_create(thread, start_process, filename);	//初始化线程栈结构体 struct thread_stack
   thread->pgdir = create_page_dir();				//【进程新增】创建用户进程，页目录表
   
   enum intr_status old_status = intr_disable();
   ASSERT(!elem_find(&thread_ready_list, &thread->general_tag));
   list_append(&thread_ready_list, &thread->general_tag);
   ASSERT(!elem_find(&thread_all_list, &thread->all_list_tag));
   list_append(&thread_all_list, &thread->all_list_tag);
   intr_set_status(old_status);
}
注意：给用户进程创建页表
```

```c
/*用户进程执行过程*/
schedule
	process_activate	//【进程新增】1.激活线程或进程的页表,【更新CR3寄存器】
						//2.用户进程，更新tss中的esp0为进程的特权级0的栈
	switch_to
kernel_thread(start_process, user_prog);  //kernel_thread(function, args)
start_process(user_prog); //【进程新增】构建进程初始上下文（填充中断栈/0级栈），利用iretd填充到CPU中
intr_exit
user_prog
```

```c
/*主要介绍下边的函数*/
void start_process(void* filename_) {
	void* function = filename_;
	struct task_struct* cur = running_thread();
	cur->self_kstack += sizeof(struct thread_stack);
	struct intr_stack* proc_stack = (struct intr_stack*)cur->self_kstack;	 
	proc_stack->edi = proc_stack->esi = proc_stack->ebp = proc_stack->esp_dummy = 0;
	proc_stack->ebx = proc_stack->edx = proc_stack->ecx = proc_stack->eax = 0;
	/* 在iretd返回时，如果发现未来的 CPL（也就是内核栈中 CS.RPL，或者说是返回到的用户进程的代码段CS.CPL）
	 * 权限低于（数值上大于） CPU 中段寄存器（如 DS、 ES、 FS、 GS）中选择子指向的内存段的 DPL，
	 * CPU 会自动将相应段寄存器的选择子置为 0 
	 * 这里gs的选择子在iretd返回后，会被自动清零。*/
	proc_stack->gs = 0;					// 用户态用不上,直接初始为0
	proc_stack->ds = proc_stack->es = proc_stack->fs = SELECTOR_U_DATA;		// 用户级数据段
	proc_stack->eip = function;			// 待执行的用户程序地址
	proc_stack->cs = SELECTOR_U_CODE;	// 用户级代码段
	proc_stack->eflags = (EFLAGS_IOPL_0 | EFLAGS_MBS | EFLAGS_IF_1);
	
	/* 给用户进程创建 3 特权级栈，它是在谁的内存空间中申请的？是安装在谁的页表中了？
	 * create_page_dir  为用户进程创建页目录表
	 * process_activate 使用户进程页表生效【已经把CR3寄存器 更新为 用户进程的页表了】
	 * start_process    为用户进程创建3特权级栈【所以申请的内存空间来自用户进程的页表】
	 * 
	 * USER_STACK3_VADDR=(0xc0000000 - 0x1000)
	 */
	proc_stack->esp = (void*)((uint32_t)get_a_page(PF_USER, USER_STACK3_VADDR) + PG_SIZE) ;
	proc_stack->ss = SELECTOR_U_DATA;
	
	/* 将 esp 替换为 proc_stack
	 * 通过 jmp intr_exit 那里的一系列 pop 指令和 iretd 指令，
	 * 将 proc_stack 中的数据载入 CPU 的寄存器，
	 * 从而使程序“假装”退出中断，进入特权级 3
	 */
	asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (proc_stack) : "memory");
}
注意：给用户进程创建 3 特权级栈
```

&nbsp;

### 进程切换关键点

关键点 1：从中断返回，必须要经过 intr_exit，即使是“假装”。
从中断返回肯定要用到 iretd 指令， iretd 指令会用到栈中的数据作为返回地址，
还会加载栈中 eflags 的值到 eflags 寄存器，
如果栈中 cs.rpl 若为更低的特权级，处理器的特权级检查通过后，
会将栈中 cs 载入到 CS 寄存器，栈中 ss 载入 SS 寄存器，随后处理器进入低特权级。
因此我们必然要在栈中提前准备好数据供 iretd 指令使用。
咱们将进程的上下文都存到栈中，通过一系列的 pop 操作把用户进程的数据装载到寄存器，
最后再通过 iretd 指令退出中断，把退出中断彻底地“假装”一回。

关键点 2：必须提前准备好用户进程所用的栈结构，
在里面填装好用户进程的上下文信息，借一系列 pop 出栈的机会，
将用户进程的上下文信息载入 CPU 的寄存器，为用户进程的运行准备好环境。

关键点 3：我们要在栈中存储的 CS 选择子，其 RPL 必须为 3。
CPU 是如何知道从中断退出后要进入哪个特权级呢？
这是由栈中保存的 CS 选择子中的 RPL 决定的，
CS.RPL 就是 CPU 的 CPL，当执行 iretd 时，在栈中保存的 CS 选择子要被加载到代码段寄存器 CS 中，
因此栈中 CS 选择子中的 RPL 便是从中断返回后 CPU 的新的 CPL。

关键点 4，栈中段寄存器的选择子必须指向 DPL 为 3 的内存段。
既然用户进程的特权级为 3，操作系统有责任把用户进程所有段选择子的 RPL 都置为 3，
这样，在 RPL=CPL=3 的情况下，用户进程只能访问 DPL 为 3 的内存段，即代码段、数据段、栈段。
我们前面的工作中已经准备好了 DPL 为 3 的代码段及数据段。

关键点 5：必须使栈中 eflags 的 IF 位为 1。
对于可屏蔽中断来说， 任务之所以能进入中断， 是因为标志寄存器 eflags 中的 IF 位为 1， 退出中断后，还得保持 IF 位为 1，继续响应新的中断。

关键点 6：必须使栈中 eflags 的 IOPL 位为 0。
用户进程属于最低的特权级，对于 IO 操作，不允许用户进程直接访问硬件，只允许操作系统有直接的硬件控制。
这是由标志寄存器 eflags 中 IOPL 位决定的，必须使其值为 0。

&nbsp;

## 系统调用、printf、堆内存管理

### 系统调用

int syscall(int number, …) 是glibc的库函数。

_syscallX(type,name,type1,arg1,type2,arg2,…)是系统调用。

当参数多于 5 个时，可以用内存来传递。

此时在内存中存储的参数仅是第 1 个参数及第 6 个以上的所有参数，不包括第 2～5 个参数，

第 2～5 个参数依然要顺序放在寄存器 ecx、 edx、 esi 及 edi 中，

eax 始终是子功能号。

&nbsp;

思路：

（1）用中断门实现系统调用，效仿 Linux 用 0x80 号中断作为系统调用的入口。

（2）在 IDT 中安装 0x80 号中断对应的描述符，在该描述符中注册系统调用对应的中断处理例程。

（3）建立系统调用子功能表 syscall_table，利用 eax 寄存器中的子功能号在该表中索引相应的处理函数。

（4）用宏实现用户空间系统调用接口 **_syscall**，最大支持 3 个参数的系统调用，故只需要完成_syscall[0-3]。

寄存器传递参数， eax 为子功能号， ebx保存第 1 个参数， ecx 保存第 2 个参数， edx 保存第 3 个参数。

&nbsp;

get_pid对应用户层库实现的

sys_getpid对应内核实现的部分

&nbsp;

### printf

可变长度参数，本质上还是编译时确定的，是静态的（相对于C可变长数组是堆分配的，运行时确定的"动态"）

这一切得益于编译器采用 C 调用约定来处理函数的传参方式。 

C调用约定规定：由调用者把参数以从右向左的顺序压入栈中，并且由调用者清理堆栈中的参数。 

既然参数是由调用者压入的，调用者当然知道栈中压入了几个参数，参数占用了多少空间，

因此无论函数的参数个数是否固定，采用 C 调用约定，调用者都能完好地回收栈空间，不必担心栈溢出等问题。 

&nbsp;

ap（argument pointer）是指针变量，指向可变参数在栈中的地址。

ap 的类型为 va_list，va_list 本质上是指针类型，是 void* 或 char* 都可以。

 （1） va_start(ap,v)，参数 v 是支持可变参数的函数的第 1 个参数（比如printf的format字符串）。

此宏功能是，使指针 ap 指向 v 的地址，相当于初始化 ap 指针。

（2） va_arg(ap,t)，参数 t 是可变参数的类型。

此宏的功能是，使指针 ap 指向栈中下一个参数的地址并返回其值。

（3） va_end(ap)，将 ap 置为 null。

&nbsp;

### 堆内存管理

一次分配4kB内存太大，需要分配小块内存，因此引入malloc/free

提前建立好多种不同大小的内存块的仓库（arena）



&nbsp;

## 硬盘驱动

啊

## 文件系统

啊

## 交互系统





















啊