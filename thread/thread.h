#ifndef __THREAD_THREAD_H
#define __THREAD_THREAD_H
#include "stdint.h"
#include "list.h"
#include "bitmap.h"
#include "memory.h"

#define MAX_FILES_OPEN_PER_PROC 8
/* 自定义通用函数类型 */
typedef void thread_func(void*);
typedef int16_t pid_t;

/* 进程或线程的状态 */
enum task_status {
	TASK_RUNNING,
	TASK_READY,
	TASK_BLOCKED,
	TASK_WAITING,
	TASK_HANGING,
	TASK_DIED
};

/***********   中断栈intr_stack   ***********
 * 此结构用于中断发生时保护程序(线程或进程)的上下文环境:
 * 进程或线程被外部中断或软中断打断时,会按照此结构压入上下文寄存器, 
 * 结构体的寄存器，完全就是 Kernel.S 压入的寄存器
 * intr_exit中的出栈操作是此结构的逆操作
 * 
 * 初始情况下此栈在线程自己的内核栈中位置固定，在 PCB 所在页的最顶端，
 * 每次进入中断时就不一定了，如果进入中断时不涉及到特权级变化，它的位置就会在当前的 esp 之下，
 * 否则处理器会从 TSS 中获得新的 esp 的值，然后该栈在新的 esp 之下
********************************************/
struct intr_stack {
	uint32_t vec_no;		// kernel.S 宏VECTOR中push %1压入的中断号【最后压入】
	uint32_t edi;
	uint32_t esi;
	uint32_t ebp;
	uint32_t esp_dummy;		// 虽然pushad把esp也压入,但esp是不断变化的,所以会被popad忽略
	uint32_t ebx;
	uint32_t edx;
	uint32_t ecx;
	uint32_t eax;
	uint32_t gs;
	uint32_t fs;
	uint32_t es;
	uint32_t ds;

	/* 以下由cpu从低特权级进入高特权级时，CPU自动压入 */
	uint32_t err_code;		// err_code会被压入在eip之后
	void (*eip) (void);
	uint32_t cs;
	uint32_t eflags;
	void* esp;
	uint32_t ss;			// 因为要切换到高特权级栈，所以旧栈ss:esp要压入【第一个压入】
};

/***********  线程栈thread_stack  ***********
 * 线程自己的栈
 * 1.用于存储线程中待执行的函数
 * 此结构在线程自己的内核栈中位置不固定,
 * 2.用在switch_to时保存线程环境。
 * 实际位置取决于实际运行情况。
 ******************************************/
struct thread_stack {
	/* ABI规定 ebp/ebx/edi/esi/esp 归主调函数所用
	 * 被调函数执行完后，这5个寄存器不允许被改变  
	 * 在 switch_to 中，一上来先保存这几个寄存器  */
	uint32_t ebp;				// 【低地址处】【最后压入】
	uint32_t ebx;
	uint32_t edi;
	uint32_t esi;

	/* 线程第一次执行时 eip 指向待调用的函数 kernel_thread
	 * 其它时候 eip 指向 switch_to 任务切换后新任务的返回地址 */
	void (*eip) (thread_func* func, void* func_arg);

	/* 以下仅供第一次被调度上cpu时使用
	 * 根据call指令，即函数调用过程的压栈情况：
	 * 进入到函数 kernel_thread 时，
	 * 栈顶处是返回地址，
	 * 栈顶+4 的位置保存的参数是 function，
	 * 栈顶+8 保存的参数是 func_arg。
	 *
	 * kernel_thread 函数并不是通过调用 call 指令的形式执行的，
	 * 而是用 ret “返回”执行的
	 * 函数 kernel_thread 作为“某个函数”（此函数暂时为 thread_start）的返回地址，
	 * 通过 ret 指令使函数 kernel_thread 的偏移地址（段基址为 0）被载入到处理器的 EIP 寄存器，
	 * 从而处理器开始执行函数 kernel_thread。
	 * 由于它的执行并不是通过 call 指令实现的，所以进入到函数 kernel_thread 中执行时，栈中并没有返回地址。
	 * 
	 * 灵活运用 ret 指令，在没有 call 指令的前提下，直接在栈顶装入函数的返回地址，再利用 ret 指令执行该函数。
	 *
	 * 为了满足 C 语言的调用形式，使 kernel_thread 以为自己是通过“正常渠道”，也就是 call 指令调用执行的，当前栈顶必须得是返回地址。
	 * 故参数 unused_ret 只为占位置充数，由它充当栈顶，其值充当返回地址，所以它的值是多少都没关系，因为咱们将来不需要通过此返回地址“返回”
	 * 咱们的目的是让 kernel_thread 去调用 func(func_arg)，也就是“只管继续向前执行”就好了，此时不需要“回头”。
	 * 总之我们只要保留这个栈帧位置就够了，为的是让函数 kernel_thread 以为栈顶是它自己的返回地址，
	 * 这样便有了一个正确的基准，并能够从栈顶+4 和栈顶+8 的位置找到参数 func 和 func_arg。
	 * 否则，若没有占位成员 unused_ret 的话，处理器依然把栈顶当作返回地址作为基准，以栈顶向上+4 和+8 的地方找参数 func 和 func_arg，
	 * 但由于没有返回地址，此时栈顶就是参数 func，栈顶+4 就是 func_arg，栈顶+8 的值目前未知，因此处理器便找错了栈帧位置，后果必然出错。
	 * 注意，这里所说的“只管继续向前执行”，只是函数第一次在线程中执行的情况，即前面所说的栈thread_stack 的第 1 个作用。
	 * 在第 2 个作用中，会由调度函数 switch_to 为其留下返回地址，这时才需要返回。
	 */
	void (*unused_retaddr);	// unused_ret只为占位置充数，为“返回地址”保留位置
	thread_func* function;	// 由 kernel_thread 函数调用的函数名
	void* func_arg;			// 由 kernel_thread 函数调用 function 时的参数【高地址处】
};

/* 进程或线程的 PCB 程序控制块 */
/* 中断栈 intr_stack 和线程栈 thread_stack 都位于线程的内核栈中，也就是都位于 PCB 的高地址处。*/
struct task_struct {
	/* self_kstack 是各线程的内核栈，栈顶指针！
	 * 当线程被创建时， self_kstack 被初始化为自己 PCB 所在页的顶端。
	 * 之后在运行时，
	 * 在被换下处理器前，我们会把线程的上下文信息保存在 0 特权级栈中，
	 * self_kstack 便用来记录 0 特权级栈在保存线程上下文后，新的栈顶，
	 * 在下一次此线程又被调度到处理器上时，
	 * 把 self_kstack 的值加载到 esp 寄存器，这样便从 0 特权级栈中获取了线程上下文，从而可以加载到处理器中运行。
	 */
	uint32_t* self_kstack;		// 各内核线程都用自己的内核栈
	pid_t pid;
	enum task_status status;
	char name[16];
	uint8_t priority;			// 换下CPU时，将 priority 赋值给 ticks
	uint8_t ticks;				// 每次在处理器上执行的时间嘀嗒数

	/* 此任务自上cpu运行后至今占用了多少cpu嘀嗒数,
	 * 也就是此任务执行了多久*/
	uint32_t elapsed_ticks;

	int32_t fd_table[MAX_FILES_OPEN_PER_PROC];	// 文件描述符数组，线程最多打开8个文件

	/* 线程在"就绪队列"中的结点 */
	struct list_elem general_tag;
	/* 线程在"全部队列"中的结点 */
	struct list_elem all_list_tag;

	uint32_t* pgdir;						// 进程自己页目录表的虚拟地址，加载到cr3时需转成物理地址
	struct virtual_addr userprog_vaddr;		// 用户进程的虚拟地址池，内核进程的定义在memory.c中
	struct mem_block_desc u_block_desc[DESC_CNT];   // 用户进程内存块描述符
	
	/* PCB 和 0 级栈是在同一个页中，栈位于页的顶端并向下发展，
	 * 因此担心压栈过程中会把 PCB 中的信息给覆盖，
	 * 所以每次在线程或进程调度时，要判断是否触及到了进程信息的边界， */
	uint32_t stack_magic;		// 用这串数字做栈的边界标记,用于检测栈的溢出
};

extern struct list thread_ready_list;
extern struct list thread_all_list;

void thread_create(struct task_struct* pthread, thread_func function, void* func_arg);
void init_thread(struct task_struct* pthread, char* name, int prio);
struct task_struct* thread_start(char* name, int prio, thread_func function, void* func_arg);
struct task_struct* running_thread(void);
void schedule(void);
void thread_init(void);
void thread_block(enum task_status stat);
void thread_unblock(struct task_struct* pthread);
void thread_yield(void);
#endif
