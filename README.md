# x86_os

learn 《真象还原》

&nbsp;

逻辑扇区号：mbr=0, loader=2, kenerl=9, 用户应用程序=300, 之后的应用程序写到 400/500...

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

-Wmissing-prototypes 要求函数必须有声明，否则编译时发出警告。 

&nbsp;

-Wsystem-headers

Print warning messages for constructs found in system header files. **Warnings from system headers are normally suppressed**, on the assumption that they usually do not indicate real problems and would only make the compiler output harder to read.

说实话，还是没明白这个。

&nbsp;

在15章编译用户程序prog_arg时，遇到错误：

**fs.h中 SEEK_SET=1, expected identifier before numeric constant**

原因是有头文件定义过SEEK_SET，不用问，一定是系统的头文件，

gcc加上-v查看详细信息，果然，搜索路径里面有系统头文件的路径。

因此引入下边的两个选项：

-nostdinc

不搜索默认路径头文件

 -nostdlib

不使用标准库

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

【页表图】

![页表](https://github.com/WM-CH/x86_os/raw/master/%E9%A1%B5%E8%A1%A8.png)



&nbsp;

&nbsp;

## 虚拟内存布局

```
|
|
+---------0xC009A000---------位图地址（位图占4个页，管理512M内存，往高地址处使用）
|
+0xC009B000
| ··· ··· 4个页大小的位图，管理512M内存
+0xC009C000
| ··· ··· 512M是本系统最大的内存空间
+0xC009D000
|
+---------0xC009E000---------内核主线程PCB（往高地址处使用）
|
+---------0xC009F000---------内核主线程栈顶（先自减/再入栈，往低地址处使用）
|
| ··· ··· 装载内核，约70k=0x11800
|
+---------0xC0100000---------K_HEAP_START 跨过1M(0x10_0000)，使虚拟内存地址连续
|
|
```

&nbsp;

## 保护

保护模式对内存段的保护：选择子、代码段数据段、栈段

调用门过程的保护（RPL）

+跳转call/jmp时的保护

中断过程的保护



&nbsp;

## 中断（自动压栈+手动压栈）

着重看 7.4.1 中断时的保护
下边是 7.4.2 中断时的`自动压栈`
1）如果发生了特权级转移，比如被中断的进程是 3 特权级，中断处理程序是 0 特权级，
此时要把低特权级的栈段选择子 ss 及栈指针 esp 保存到栈中。
2）压入标志寄存器 eflags
3）压入返回地址，先压入 cs，后压入 eip
4）如果此中断没有相应的错误码，至此，处理器把寄存器压栈的工作完成.
如果此中断有错误码的话，处理器在压入 eip 之后，会压入错误码
5）如果栈中有错误码，在 iret 指令执行前必须要把栈中的错误码跨过！！！
 此后开始使用 0 特权级的栈（ss/esp）

&nbsp;

以上是硬件自动处理的，在kernel.S的中断处理函数中，我们`手动压栈`的情况：

```
;宏 VECTOR 接受两个参数，使用时用%1/%2表示
%macro VECTOR 2
section .text
intr%1entry:		; 每个中断处理程序都要压入中断向量号（第一个参数%1）；intr%1entry在宏参数替换之后，就是个标号，标号就是个地址
	%2				; 会被展开成 nop，或者是 push 0【错误码的统一处理】
	; 以下是保存上下文环境
	push ds
	push es
	push fs
	push gs
	pushad			; PUSHAD指令压入32位寄存器,其入栈顺序是: EAX,ECX,EDX,EBX,ESP,EBP,ESI,EDI
...
```

在switch_to中，我们`手动压栈`的情况：

```
[bits 32]
section .text
global switch_to
; 这样被调用的 switch_to(cur, next); 两个参数是两个线程的PCB
; switch_to时的栈：
; next		<-- esp+24
; cur		<-- esp+20
; 返回地址	<-- esp+16
; esi		<-- esp+12
; edi		<-- esp+8
; ebx		<-- esp+4
; ebp 		<-- esp
switch_to:
   ;栈中此处是返回地址
   push esi
   push edi
   push ebx
   push ebp

   mov eax, [esp + 20]	; 得到栈中的参数 cur = [esp+20]
   mov [eax], esp		; 保存pcb第一个成员：栈顶指针esp，即task_struct(pcb)的self_kstack字段
;------------------  以上是备份当前线程的环境，下面是恢复下一个线程的环境  ----------------
; 在同一次 switch_to 的调用执行中，
; 之前保存的寄存器属于当前线程 cur
; 之后恢复的寄存器属于下一个线程 next
;------------------------------------------------------------------------------------------
   mov eax, [esp + 24]	; 得到栈中的参数 next = [esp+24]
   mov esp, [eax]		; pcb的第一个成员是self_kstack字段，它就是0级栈顶指针
						; 用来上cpu时恢复0级栈，0级栈中保存了进程或线程所有信息,包括3级栈指针
   pop ebp
   pop ebx
   pop edi
   pop esi
   ret					; 返回到上面switch_to第一行那句注释的返回地址,
						; 1.如果线程是第一次执行，则会返回到kernel_thread，是在thread_create中设置的
						; 2.如果线程执行过了，则会返回到schedule函数，再返回到intr_timer_handler函数
						; 再返回到kernel.S中的jmp intr_exit 从而恢复任务的全部寄存器映像，
						; 之后通过 iretd 指令退出中断，任务被完全彻底地恢复。
```

&nbsp;

## 线程切换

1.理解thread.c中 thread_start 中 ret，它实现的线程切换。

thread_start 给普通线程，分配一个页，作为PCB。然后调用 init_thread 和 thread_crete。

- init_thread 给普通线程，填充PCB，其中将 self_kstack 赋值为PCB最顶端！
- thread_create 给普通线程，预留出"中断栈intr_stack"空间，预留出"线程栈thread_stack"并赋值。注意 self_kstack 的位置：

```
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
```

本次演示 thread_start 中 调用内联汇编进行任务切换。

ret 之前，使 thread->self_kstack 的值作为栈顶，获取了栈顶指针，pop出去4个寄存器，此时 esp指向了"线程栈thread_stack"成员eip

ret使当前esp赋值给eip，调用到 kernel_thread 函数

在 kernel_thread 函数中时栈的内容是：

```
; fun_arg
; function
; unused_retaddr		<--- esp
```

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

参考【虚拟内存布局】一节。

&nbsp;

- 主线程执行时，发生时钟中断并切换线程。

kernel.s中的 intr%1entry 保护现场，栈中保存一个"中断栈" intr_stack的内容，

调用时钟中断函数 intr_timer_handler

调用thread.c中schedule的调度

调用switch.s中的switch_to

switch_to(cur, next); 两个参数是两个线程的PCB

调用switch_to时"主线程"的栈：

```
; 前边还有很多压栈的数据，包括主线程中断之前压栈的一些返回地址，中断栈，以及intr_timer_handler、schedule和switch_to的返回地址。
; next <---- esp+24
; cur <---- esp+20
; 返回地址 <---- esp+16
在switch_to中根据ABI规定，压入"cur线程"的4个寄存器
; esi <---- esp+12
; edi <---- esp+8
; ebx <---- esp+4
; ebp <---- esp 赋值给 self_kstack
```

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

```
; 前边还有很多压栈的数据，包括一个中断栈，以及intr_timer_handler、schedule和switch_to的返回地址。
; next <---- esp+24
; cur <---- esp+20
; 返回地址 <---- esp+16
在switch_to中根据ABI规定，压入"cur线程"的4个寄存器
; esi <---- esp+12
; edi <---- esp+8
; ebx <---- esp+4
; ebp <---- esp 赋值给 self_kstack
```

最后将栈顶指针esp赋值给 => PCB的第一个成员"线程内核栈栈顶指针"self_kstack

下边恢复主线程，

从next的PCB获取到 self_kstack ，将它内容作为栈顶esp

之后 pop 4个寄存器、调用ret将esp赋值给eip。这次是返回到了switch_to的调用者 schedule 

然后退回到 intr_timer_handler，intr_exit

接着被中断前的位置继续执行了。。。

&nbsp;

### self_kstack

是各线程的内核栈，栈顶指针！
当线程被创建时， self_kstack 被初始化为自己 PCB 所在页的顶端。
之后在运行时，
在被换下处理器前，我们会把线程的上下文信息保存在 0 特权级栈中，
self_kstack 便用来记录 0 特权级栈在保存线程上下文后，新的栈顶，
在下一次此线程又被调度到处理器上时，
把 self_kstack 的值加载到 esp 寄存器，这样便从 0 特权级栈中获取了线程上下文，从而可以加载到处理器中运行。

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

```
thread_start(...,function,...)
thread = get_kernel_pages(1)
init_thread(thread,...)
thread_create(thread,function,...)
kernel_thread(function, args)
function
```

把 function 替换为创建进程的新函数。

&nbsp;

### 1> self_kstack的位置

答：是进程初始化之后，self_kstack的位置没动，见线程一节的描述。

### 2> 0/3特权级栈的位置

2.1> 用户进程3特权级栈在 **USER_STACK3_VADDR=(0xc0000000 - 0x1000)**

```
在 4GB 的虚拟地址空间中
(0xc0000000-1)是用户空间的最高地址，0xc0000000～0xffffffff 是内核空间。

命令行参数和环境变量也是被压栈的
所以栈底是0xc000_0000
栈顶是0xc0000_0000 - 0x1000
+----------------------+ --栈底 0xc0000_0000 - 1
| 命令行参数和环境变量 |
+----------------------+
|      特权级3的栈     |
+----------------------+
|          ↓           |
|                      | --栈顶 0xc0000_0000 - 0x1000【start_process函数】
|                      |
|          ↑           |
+----------------------+
|          堆          |
+----------------------+
|          bss         |
+----------------------+
|          data        |
+----------------------+
|          text        |
+----------------------+ 0
    C程序内存布局
```

用户进程的页表、虚拟地址位图、PCB都在内核的内存中分配和管理。

&nbsp;

2.2> 用户进程0特权级栈在 **PCB + PG_SIZE**

见虚拟内存布局

内核线程：

PCB地址0xc009e000（往高地址增长，有magic数和栈做间隔）

0特权级栈0xc009f000（先自减，再压栈）

用户线程：

PCB地址在thread.c中thread_start函数里面，通过get_kernel_pages分配。

PCB结构和0特权级栈是在同一个页的。

```
init_thread：
一开始指向0特权级栈顶
pthread->self_kstack = (uint32_t*)((uint32_t)pthread + PG_SIZE);
pthread就是PCB的起始地址

thread_create：
后来预留出两个空间，一个是中断栈，一个线程内核栈
pthread->self_kstack -= sizeof(struct intr_stack);
pthread->self_kstack -= sizeof(struct thread_stack);
```

&nbsp;

### 3> update_tss_esp函数

只用了一个全局变量g_tss，怎么区分内核进程和用户进程的？

答：update_tss_esp函数只会被用户进程调用。

```
/* 更新tss中esp0字段的值为pthread的0级栈 */
void update_tss_esp(struct task_struct* pthread) {
	g_tss.esp0 = (uint32_t*)((uint32_t)pthread + PG_SIZE);
}
```

&nbsp;

### 4> 因为PCB和0级栈在同一个page里面！

不管是内核线程，还是用户线程，都是如此！

```
/* 返回线程 PCB 地址。
 * 各个线程所用的 0 级栈都是在自己的 PCB 当中，
 * 取当前栈指针的高 20 位，就是当前运行线程的 PCB（PCB是在自然页的起始地址！）
 */
struct task_struct* running_thread() {
	uint32_t esp;
	asm ("mov %%esp, %0" : "=g" (esp));	//esp寄存器的值，放入变量esp
	return (struct task_struct*)(esp & 0xfffff000);
}
```

&nbsp;

### 5> readelf 选项

选项 -e,headers 显示全部头信息，等价于: -h -l -S 。

其中数据段的fileSize和memSize不相等，因为有BSS段。

&nbsp;

### 进程创建/切换流程

给用户进程创建内存位图，使用地址是：linux用户程序入口地址 `0x80480000`

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
    /* 中断栈中，上边几个是中断处理函数手动压栈的，下边几个是硬件自动压栈的 */
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

【arena图】

![1630166866548](https://github.com/WM-CH/x86_os/raw/master/arena.png)



&nbsp;

## 硬盘驱动

3.5 一节的硬盘介绍

硬盘寻道时间，转速7200转/分钟。

磁头移动不是直来直去，而是圆弧。寻找数据只跟终点有关，路径无所谓。

Integrated Drive Electronics，集成设备电路， IDE接口。也叫 ATA 接口Advanced Technology Attachment 

硬盘串行接口 Serial ATA， SATA 出来后，

IDE 改称为 Parallel ATA， PATA。 

SATA口是L形的，

PATA口是一个长条，一个PATA口也叫一个IDE口，可以接两个硬盘，叫master/slave。

一个主板上支持2个PATA口，分别叫 IDE0，IDE1，也叫primary通道，secondary通道。

物理上是用“柱面-磁头-扇区”来定位的（ Cylinder Head Sector），简称为 CHS

逻辑上是逻辑块地址（ Logical Block Address） 逻辑扇区号

&nbsp;

13章

新建硬盘hd80M.img

ata0-master: type=disk, path="hd80M.img", mode=flat, cylinders=162, heads=16, spt=63

里面的master改成slave。

【硬盘图】

![硬盘图](https://github.com/WM-CH/x86_os/raw/master/%E7%A1%AC%E7%9B%98%E7%BB%93%E6%9E%84.png)





根据上图中的公式，计算：

硬盘容量=63× 162× 512×16=83607552 字节，将其换算成 MB 为 79.734375MB，约等于 79.73MB。

ls -l 看一下文件hd80M.img可以看出它的大小是 83607552 字节。

&nbsp;

硬盘只支持4个分区。

为了扩展，分区描述符，文件系统类型id=5的，被视为**扩展分区**。【其他3个分区称为**主分区**】

最多一个扩展分区，ide可以有63个**子扩展分区**、scsi可以有15个。

扩展分区是种抽象、不具实体的分区，它需要再被划分出子分区，也就是所谓的**逻辑分区**，

逻辑分区才可以像其他主分区那样使用。

扩展分区的第一个逻辑分区，编号从5开始。

&nbsp;

fdisk命令中，x和c设置柱面为162，x和h设置磁头数为16，r返回上一级。

分区方式：

一个主分区，编号1，柱面1~32

一个扩展分区，编号4，柱面33~全部

5个逻辑分区，柱面33~50、51~75、76~90、91~120、121~162

l命令查看文件系统类型id号

t命令设置分区文件系统id，5个逻辑分区全部设置为0x66

不知道为啥，指定分区的起止柱面时，fdisk让我选的是起止扇区号，，，而且是从2048开始的，网上说是为了EFI。。。找了个汇编文件，编译之后生成了hd80M.img

&nbsp;

**MasterBootRecord主引导记录/Extend扩展引导记录**

MBR引导扇区/EBR引导扇区，包含：

- 446字节（参数+引导程序）
- 64字节**磁盘分区表DiskPartitionTable**，每个分区描述符16B，共有4个**主分区**
- 0x55AA

&nbsp;

MBR在主引导扇区，0盘0道1扇区，LBA从0开始。

MBR只占一个扇区，但是分区是以柱面为粒度的，

故分区起始从63扇区开始，前边的1~62号扇区只能空着不用。

&nbsp;

子扩展分区结构和硬盘一样，第一个扇区类似于MBR，他叫EBR。

硬盘结构    =MBR引导扇区 + 空闲扇区 + 主分区/扩展分区所在的空间

子扩展分区=EBR引导扇区 + 空闲扇区 + 逻辑分区

EBR中的4个DPT，

第一个分区表项：指向逻辑分区最开始的扇区，此扇区称为**操作系统引导扇区，OBR，内核加载器，DosBR**）

第二个分区表项：指向下一个子扩展分区的EBR扇区

第三、四个分区表项，没用到。

&nbsp;

MBR在硬盘最开始。EBR在子扩展分区最开始。

MBR/EBR不属于分区之内，操作系统能修改，但一般不会故意修改，可视为在操作系统管理之外。

OBR位于逻辑分区最开始的扇区，操作系统能管理。OBR中没有分区表。

&nbsp;

**活动分区**是指引导程序所在的分区，活动分区标记0x80是给 MBR看的，它通过此位来判断该分区的引导扇区中是否有引导程序 OBR。

&nbsp;

【分区表项结构体参数】

![分区表项](https://github.com/WM-CH/x86_os/raw/master/%E5%88%86%E5%8C%BA%E8%A1%A8%E9%A1%B9%E7%BB%93%E6%9E%84%E4%BD%93%E5%8F%82%E6%95%B0.png)

重点关注“分区起始偏移扇区”和“分区容量扇区数” 。

偏移扇区的基准：

逻辑分区的基准是子扩展分区的起始扇区 LBA 地址。 

子扩展分区的基准是总扩展分区的起始扇区 LBA 地址。 

主分区或总扩展分区，基准为 0。 

&nbsp;

【分区布局汇总】

![分区布局](https://github.com/WM-CH/x86_os/raw/master/%E5%88%86%E5%8C%BA%E5%B8%83%E5%B1%80%E6%B1%87%E6%80%BB.png)

&nbsp;

硬盘驱动程序，略。

&nbsp;

读取分区表

**identify 命令获得的返回信息（部分） **

| 字 偏 移 量 | 描 述                                 |
| ----------- | ------------------------------------- |
| 10～19      | 硬盘序列号，长度为 20 的字符串        |
| 27～46      | 硬盘型号，长度为 40 的字符串          |
| 60～61      | 可供用户使用的扇区数，长度为 2 的整型 |

硬盘命名规则是`[x]d[y][n]` 

x 硬盘分类：h 代表 IDE 磁盘， s 代表 SCSI 磁盘。
d 表示 disk，即磁盘。
y 表示设备号，区分第几个设备，小写字符 a 是第 1 个硬盘， b 是第 2 个硬盘，，，
n 表示分区号，从数字1开始

&nbsp;

## 文件系统

攒到“足够大小”时才一次性访问硬盘，这足够大小的数据就是块，块是扇区整数倍。

在 Windows 中，块被称为簇，格式化FAT32分区时，可以选择不同大小的簇，有 4KB、 32KB。

块是文件系统的读写单位，因此文件至少要占据一个块，大于 1 个块的文件被拆分成多个块来存储。

多个块该如何组织到？

### FAT 文件分配表

此文件系统中的文件所有的块，是链式的，每个块的最后存储下一个块的地址。

链式管理，每次访问必须从头开始遍历， 遍历的每一个结点，都涉及一次硬盘寻道。

很烂，后来微软推出了NTFS。

### UNIX的inode

索引结构的文件系统，文件中的块依然分散到不连续的零散空间，

更重要的是文件系统为每个文件的所有块，建立了一个**索引表**，索引表就是块地址数组。

索引表存储在**inode，即index node**，索引结点中。

一个文件必须对应一个inode

缺点是索引表本身要占用空间。

索引表的结构：

每个索引表 15 个索引项（**老表**）

前 12 个索引项是文件的前 12 个块的地址。

若文件大于 12 个块，就建立个**一级间接块索引表**，表地址放到第**13**个索引项。

一间表中可容纳 256 个块的地址，

这 256 个块地址要通过一间表才能找到，因此称为**间接块**，

有了一间表，文件最大可达 `12+256=268` 个块。 

同理，**二间表**存储在老表第**14**个索引项，文件最大可达`12+256+256*256`个块

**三间表**存储在老表第**15**个索引项，文件最大可达`12+256+256*256+256*256*256`个块

再大，就只能切割成小文件存储了。

![inode结构和间接块索引表](https://github.com/WM-CH/x86_os/raw/master/inode%E7%BB%93%E6%9E%84%E5%92%8C%E9%97%B4%E6%8E%A5%E5%9D%97%E7%B4%A2%E5%BC%95%E8%A1%A8.png)

如图，**inode结构**中

i 结点编号：在 inode 数组中的下标

权限：读、写、执行

属主：文件的拥有者

时间：创建时间、修改时间、访问时间

文件大小：文件的字节尺寸

用于管理、控制文件相关信息的被称为**FCB（File Contrl Block）文件控制块**。inode是 FCB 的一种。

我们只完成图中灰色部分。

"所有文件的 inode 结构"使用的磁盘空间会在格式化时确定，它决定了**分区最大的文件数**。tune2fs -l命令查看。

一个分区的利用率分为**inode 的利用率**、**磁盘空间利用率**两种，

df –i 命令查看 inode 利用率，不加参数的 df 时，查看的是空间利用率。 

inode可能用完了但是空间并不满；空间如果满了inode一定满了。

### 目录项

在Linux中目录和文件都用inode来表示，称为**目录文件**。

**目录项**

```
book[book-virtual-machine] /home/book $ ls -lai /lib
inode号  属性
131073 drwxr-xr-x 23 root root  4096 8月   7 18:36 .
     2 drwxr-xr-x 28 root root  4096 8月  30 21:52 ..
131099 -rwxr-xr-x  1 root root 70952 11月 15  2017 klibc.so
```

inode不知道数据块中是普通文件，还是目录。但是目录项知道。

目录项结构包括：文件名、inode号、文件类型。

先在目录中，找到文件的目录项，目录项中找到inode编号，在inode数组中找到inode，再找到数据块。

目录项也是FCB。

**根目录**所在数据块的地址是被“写死”的。

### 超级块与文件系统布局

文件系统是针对分区来管理的，每个分区都有一个inode数组。用inode位图来管理inode数组。

空闲块也需要位图来管理。

超级块结构：

| 魔数（判断文件系统类型） |
| ------------------------ |
| 数据块数量               |
| inode数量                |
| 分区起始扇区地址         |
| 空闲块位图地址           |
| 空闲块位图大小           |
| inode位图地址            |
| inode位图大小            |
| inode数组地址            |
| inode数组大小            |
| 根目录地址               |
| 根目录大小               |
| 空闲块起始地址           |
| ...                      |

超级块的位置必须是固定的，它在各分区的第 2 个扇区，通常占用一个扇区的大小。

![文件系统布局](https://github.com/WM-CH/x86_os/raw/master/%E6%96%87%E4%BB%B6%E7%B3%BB%E7%BB%9F%E5%B8%83%E5%B1%80.png)

引导程序可能占用多个扇区，这多个扇区组成一个数据块，因此图中标出的是引导“块”，而不是引导“扇区”。 

&nbsp;

### 格式化分区，创建文件系统

创建文件系统就是创建文件系统所需要的元信息， 包括

超级块位置及大小、 空闲块位图的位置及大小、

inode 位图的位置及大小、 inode 数组的位置及大小、

空闲块起始地址、根目录起始地址。

创建步骤如下：
（ 1）根据分区 part 大小，计算分区文件系统各元信息需要的扇区数及位置。

（ 2）在内存中创建超级块，将以上步骤计算的元信息写入超级块。

（ 3）将超级块写入磁盘。

（ 4）将元信息写入磁盘上各自的位置。

（ 5）将根目录写入磁盘。 

### 挂载分区

Linux 内核所在的分区是默认分区，该分区的根目录是固定存在的，

使用其他新分区的话，需要用 mount 命令把新的分区挂载到默认分区的某个目录下。

尽管其他分区都有自己的根目录，但是默认分区的根目录才是所有分区的父目录。

&nbsp;

我们要实现的分区挂载，功能类似 Linux 的 mount 命令，但又不完全是，

因为 mount 命令是把一个分区挂载到默认分区（操作系统所在分区）的某个目录上，

但咱们的操作系统安装到裸盘 hd60M.img 上，上面没有分区，更谈不上文件系统了。

&nbsp;

要想把操作系统安装到文件系统上，必须在实现内核之前先实现文件系统模块，至少得完成写文
件的功能，然后把操作系统通过写文件功能写到文件系统上。

操作系统安装过程中都是先选择安装到哪个分区上，然后选择以什么文件系统来格式化该分区，之后才开始正式的安装，最终操作系统就被安装到某种文件系统上了。

&nbsp;

既然操作系统不在文件系统上，也就没有默认目录，那就不用考虑分区挂载到哪个目录下，

因此咱们要实现的挂载功能很简单：直接选择待操作的分区。 

&nbsp;

挂载分区，就是把该分区文件系统的元信息从硬盘上读出来加载到内存中， 这样硬盘资源的变化都
用内存中元信息来跟踪，如果有写操作，及时将内存中的元信息同步写入到硬盘以持久化。 

&nbsp;

### 文件描述符

1.`文件结构`，文件结构的数组=`文件表`。

文件被打开几次， 就产生几个文件结构，里面有文件读写的`偏移量`。

2.`文件描述符`，只是个`整数int`，它是 PCB 中`文件描述符数组（int数组）`的下标。

文件描述符数组元素，指向文件表中的某个文件结构。 

文件描述符数组，012对应标准输入、输出、错误。

3.避免文件表占用过大的内存空间，文件结构的数量是有限的，进程可最多打开的文件数有限（ulimit 命令） 

![文件描述符&inode关系](https://github.com/WM-CH/x86_os/raw/master/%E6%96%87%E4%BB%B6%E6%8F%8F%E8%BF%B0%E7%AC%A6%26inode%E5%85%B3%E7%B3%BB.png)

4.open操作创建文件描述符：

PCB 中文件描述符数组，文件表（全局变量），inode 队列，三者都已事先构建好了

创建文件描述符的过程，就是在这三个数据结构中找空位，在该空位填充好数据后返回一个int数。





&nbsp;

## 交互系统

### fork的实现

进程的资源：

（1）进程的 pcb，即 task_struct

（2）程序体，即代码段数据段等，这是进程的实体。

（3）用户栈，用于局部变量、函数调用。

（4）内核栈，进入内核态时，一方面要用它来保存上下文环境，另一方面的作用同用户栈一样。

（5）虚拟地址池，每个进程拥有独立的内存空间，其虚拟地址是用虚拟地址池来管理的。

（6）页表，让进程拥有独立的内存空间。

新进程加入调度队列即可，但是要准备好他的**栈**，

不是0级栈，不是3级栈，是0级栈中的用户线程栈 thread_stack。



### exec的实现

1> 以前是用汇编加载的elf格式的内核，现在使用C语言方式加载elf格式的应用程序。

&nbsp;

2> 校验ELF头

2.1> 

elf 头的 e_ident 字段是 elf 格式的魔数，它是个 16 字节的数组， e_ident[7～15] 暂时未用，因此只需检测 e_ident[0～6]
	开头的 4 个字节是固定不变的，分别是 0x7f(八进制177十进制127) 和字符串“ELF”的 ascii 码
	e_ident[4] 值为 1 表示 32 位的elf，值为 2 表示 64 位
	e_ident[5] 值为 1 表示小端字节序，值为 2 表示大端字节序
	e_ident[6] 表示 elf 版本信息，默认为 1。
	e_ident[4-6] 值为 0 都是非法的
	在 8086 平台上，是小端字节序，并且是 32 位系统，因此这三位值均取 1
故 e_ident[0-6] 应分别是十六进制 0x7F、 0x45、 0x4C、 0x46、 0x1、 0x1 和 0x1

2.2>

e_type 表示目标文件类型，其值应该为 ET_EXEC，即等于 2。

e_machine 表示体系结构，其值应该为 EM_386，即等于 3。

e_version 表示版本信息，其值应该为 1。

e_phnum 用来指明程序头表中**条目的数量**，也就是段的个数，基值应该小于等于 1024。

e_phentsize 用来指明程序头表中**每个条目的字节大小**，

即每个用来描述段信息的数据结构的字节大小，该结构就是 struct Elf32_Phdr

```
/* 32位elf头 */
struct Elf32_Ehdr {
	unsigned char e_ident[16];
	Elf32_Half    e_type;		// 32 or 64 ELF
	Elf32_Half    e_machine;	// x86
	Elf32_Word    e_version;
	Elf32_Addr    e_entry;		// 入口地址（虚拟地址）
	Elf32_Off     e_phoff;		// 程序头在elf文件中的起始地址
	Elf32_Off     e_shoff;
	Elf32_Word    e_flags;
	Elf32_Half    e_ehsize;
	Elf32_Half    e_phentsize;
	Elf32_Half    e_phnum;		// 程序头表中条目的数量，也就是段的个数
	Elf32_Half    e_shentsize;	// 程序头表中每个条目的字节大小，即 描述段的数据结构的大小 == sizeof(struct Elf32_Phdr)
	Elf32_Half    e_shnum;
	Elf32_Half    e_shstrndx;
};

/* 程序头表Program header.就是段segment描述头 */
struct Elf32_Phdr {
	Elf32_Word p_type;		// PT_LOAD可加载的段，等等
	Elf32_Off  p_offset;	// 在elf文件中，segment的偏移地址
	Elf32_Addr p_vaddr;		// 在内存中，希望被加载到的地址
	Elf32_Addr p_paddr;
	Elf32_Word p_filesz;	// Segment的大小
	Elf32_Word p_memsz;		// 因为BSS段，所以 memsz 不等于 filesz
	Elf32_Word p_flags;
	Elf32_Word p_align;
};
```

&nbsp;

3> 在c语言字符串中，有些不可见字符（多是控制字符）是没法直接通过“键入字符”的方式来表示的，

因此用 ASCII 码来表示，采用“\加 3 位八进制”或“ \x 加 2 位十六进制”来表示，注意，字符“ \”不可少。

```
"\177ELF" 是八进制的177=0x7F
如果用16进制，写成
"\x7fELF" 会被编译器识别为 "x7fE"LF
因为E被认为是16进制数了
```

&nbsp;

### 用户的应用程序

用户的应用程序 prog_no_arg.c 中用到了函数 printf，

头文件 stdio.h 要指定路径 -I ../lib

在链接的时候除了要加上 stdio.o 外， 还要加上 stdio.h（也是 stdio.o）所依赖的目标文件，

包括 string.o、 syscall.o 和 assert.o。

这些目标文件都是 build 目录下的，因此一定要先编译内核

但这并不是说，用户程序的库文件依赖于内核的目标文件，

并不是用了内核的目标文件就是执行了内核的代码，

这仅仅表示用户进程中执行的代码和内核目标文件中的代码是一样的，

在内存中它们是独立的两份拷贝，互不干涉。

无论是用谁的目标文件都不重要，目标（库）文件只是系统调用的封装而已，

不同的库文件最终的出路都是相同的，都是通过系统调用发送 0x80 号中断，利用中断门连接到唯一的内核。

一定要注意目标文件的链接顺序，本着 “调用在前，定义在后”



### C 标准库和 C 运行库

C 标准库

美国国家标准协会，即 ANSI（American National Standards Institute），

规定了一套 C 函数标准接口，即 C 标准库，明确规定了每个函数的作用及原型 。

C 标准库与OS平台无关， 它就是为了实现用户程序跨OS平台而约定的标准接口，

使用户进程无论在哪个操作系统上调用同样的函数接口，执行的结果都是一样的。

C运行库

C 运行库也称为 CRT（ C RunTime library）， 它是与操作系统息息相关的，

它的实现也基于 C 标准库，因此 CRT 属于 C 标准库的扩展。

CRT 多是补充 C 标准库中没有的功能，为适配本操作系统环境而定制开发的。

因此 CRT 并不通用，只适用于在本操作系统上运行的程序。

CRT功能：

1.初始化运行环境，在进入 main 函数之前，为用户进程准备条件，传递参数。

2.当用户进程结束时， CRT 还要负责回收用户进程的资源。

例如，使用系统调用 exit 或 _exit，用户程序结束时将陷入内核，

使处理器的控制权重新回到操作系统手中，调度下个任务。

```
[bits 32]
extern	 main
section .text
global _start
_start:
   ;下面这两个寄存器，要和execv中load之后指定的寄存器一致
   push	 ebx	  ;压入argv
   push  ecx	  ;压入argc
   call  main

1.extern 声明main函数
2._start才是用户程序的真正入口
标号_start，它是链接器默认的入口符号，如果 ld 命令链接时未使用链接脚本或-e 参数指定入口符号的话，
默认会以符号_start 为程序入口。
```



ar 命令，将 string.o、 syscall.o、 stdio.o、 assert.o 和 start.o 打包成静态库文件 simple_crt.a

simple_crt.a 类似于 CRT 的作用，它就是我们所说的简陋版 C 运行库。

后面的用户程序目标文件 prog_arg.o 和它直接链接就可以了。

&nbsp;

-nostdinc 和 -nostdlib 见**编译选项**一节。

编译时，先编译内核，再进入command目录编译用户应用程序。



### wait和exit

wait的作用

wait 通常是由父进程调用的，或者说，尽管某个进程没有子进程，但只要它调用了wait 系统调用，

该进程就被认为是父进程，内核就要去查找它的子进程，

由于它没有子进程，此时 wait 会返回-1，表示其没有子进程。

如果有子进程，这时候该进程就**被阻塞**，不再运行，

内核就要去遍历其所有的子进程，查找哪个子进程退出了，

并将子进程退出时的**返回值**传递给父进程，随后将父进程唤醒。 

&nbsp;

进程间要想相互通信必须要借用内核（无论是管道、消息队列，还是共享内存等进程间通信形式，无
一例外都是借助内核这个中间人），

子进程的返回值肯定是先要交给内核，然后是父进程向内核要子进程的返回值。 

&nbsp;

进程在调用`void _exit(int status) `时就表示进程生命周期结束了，其占用的资源可以被回收了， 

pcb里面有子进程的返回值，需要传递给父进程。

所以回收 pcb 内存空间的工作是在系统调用`pid_t wait(int *status) `对应的内核代码里面。 

系统调用 wait 成功则返回子进程的 pid，失败则返回-1。 

父进程为子进程“收尸”，其实是内核为子进程“收尸”。

&nbsp;

孤儿进程

父进程先退出了，子进程还未退出，成为孤儿进程。会被所有进程的父进程init收养。

僵尸进程

子进程先退出了，父进程虽然还在运行，但是没有调用wait，不能给他收尸。

子进程的 pcb 所占的空间不能释放，自然就成了“僵尸”。 

僵尸进程是没有进程体的，因为其进程体已在调用 exit 时被内核回收了，只剩下一个 pcb 还在进程队列中。

解决方法是，kill掉父进程。

&nbsp;

总结：

exit 是由子进程调用的，表面上功能是使子进程结束运行并传递返回值给内核，

本质上是内核在幕后，将进程除 pcb 以外的所有资源都回收。

wait 是父进程调用的，表面上功能是使父进程阻塞自己，直到子进程调用 exit 结束运行，获得子进程的返回值

本质上是内核在幕后，将子进程的返回值传递给父进程并会唤醒父进程，然后将子进程的 pcb 回收。 











啊