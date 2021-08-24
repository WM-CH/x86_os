#include "thread.h"
#include "stdint.h"
#include "string.h"
#include "global.h"
#include "debug.h"
#include "interrupt.h"
#include "print.h"
#include "memory.h"

#define PG_SIZE 4096

struct task_struct* g_main_thread;    // 主线程PCB
struct list thread_ready_list;	    // 就绪队列
struct list thread_all_list;	    // 所有任务队列
static struct list_elem* g_thread_tag;// 用于临时保存队列中的线程结点

extern void switch_to(struct task_struct* cur, struct task_struct* next);

/* 返回线程 PCB 地址。
 * 各个线程所用的 0 级栈都是在自己的 PCB 当中，
 * 取当前栈指针的高 20 位，就是当前运行线程的 PCB（PCB是在自然页的起始地址！）
 */
struct task_struct* running_thread() {
	uint32_t esp;
	asm ("mov %%esp, %0" : "=g" (esp));	//esp寄存器的值，放入变量esp
	return (struct task_struct*)(esp & 0xfffff000);
}

/* 由kernel_thread去执行function(func_arg) */
static void kernel_thread(thread_func* function, void* func_arg) {
	/* 线程的首次运行是由时钟中断处理函数调用任务调度器 schedule 完成的，
	 * 进入中断后处理器会自动关中断，
	 * 因此在执行 function 前要打开中断，否则 kernel_thread 中的 function 在关中断的情况下运行，
	 * 也就是时钟中断被屏蔽了，再也不会调度到新的线程，function 会独享处理器。
	 */
	intr_enable();
	function(func_arg); 
}

/* 初始化线程栈结构体 struct thread_stack */
void thread_create(struct task_struct* pthread, thread_func function, void* func_arg) {
	/* 先预留中断使用栈的空间 
	 *（1）将来线程进入中断后，位于 kernel.S 中的中断代码会通过此栈来保存上下文。
	 *（2）将来实现用户进程时，会将用户进程的初始信息放在中断栈中。*/
	pthread->self_kstack -= sizeof(struct intr_stack);

	/* 再留出线程栈空间（unused_retaddr 所在的栈）*/
	pthread->self_kstack -= sizeof(struct thread_stack);
	struct thread_stack* kthread_stack = (struct thread_stack*)pthread->self_kstack;
	/* 下边三行就是为能够在 kernel_thread 中调用 function(func_arg)做准备。
	 * 
	 * kernel_thread 并不是通过 call 指令调用的，而是通过 ret 来执行的，
	 * 因此无法按照正常的函数调用形式传递 kernel_thread 所需要的参数，
	 * 这样调用是不行的： kernel_thread(function, func_arg)，
	 * 只能将参数放在 kernel_thread 所用的栈中，即处理器进入 kernel_thread 函数体时，
	 * 栈顶为返回地址，栈顶+4 为参数function，栈顶+8 为参数func_arg */
	kthread_stack->eip = kernel_thread;
	kthread_stack->function = function;
	kthread_stack->func_arg = func_arg;
	kthread_stack->ebp = kthread_stack->ebx = kthread_stack->esi = kthread_stack->edi = 0;
}

/* 初始化线程PCB结构体 struct task_struct*/
void init_thread(struct task_struct* pthread, char* name, int prio) {
	memset(pthread, 0, sizeof(*pthread));	//清空PCB
	strcpy(pthread->name, name);

	if (pthread == g_main_thread) {
		/* 由于把main函数也封装成一个线程,并且它一直是运行的,故将其直接设为TASK_RUNNING */
		pthread->status = TASK_RUNNING;
	} else {
		pthread->status = TASK_READY;
	}

	/* self_kstack是线程自己在内核态下使用的栈顶地址 
	 * 线程自己在 0 特权级下所用的栈，在线程创建之初，
	 * 它被初始化为线程 PCB 的最顶端，即(uint32_t)pthread + PG_SIZE。*/
	pthread->self_kstack = (uint32_t*)((uint32_t)pthread + PG_SIZE);
	pthread->priority = prio;
	pthread->ticks = prio;
	pthread->elapsed_ticks = 0;
	pthread->pgdir = NULL;
	pthread->stack_magic = 0x19870916;	  // 自定义的魔数
}

/* 创建线程 */
struct task_struct* thread_start(char* name, int prio, thread_func function, void* func_arg) {
	/* pcb都位于内核空间,包括用户进程的pcb也是在内核空间 */
	struct task_struct* thread = get_kernel_pages(1);

	init_thread(thread, name, prio);			//初始化线程PCB
	thread_create(thread, function, func_arg);	//初始化线程栈结构体
	
	ASSERT(!elem_find(&thread_ready_list, &thread->general_tag));	// 确保之前不在队列中
	list_append(&thread_ready_list, &thread->general_tag);			// 加入就绪线程队列

	ASSERT(!elem_find(&thread_all_list, &thread->all_list_tag));
	list_append(&thread_all_list, &thread->all_list_tag);			// 加入全部线程队列

	/* 在输出部分， "g" (thread->self_kstack)使 thread->self_kstack 的值作为输入，采用通用约束 g，即内存或寄存器都可以。
	 * 在汇编语句部分，movl %0, %%esp 使 thread->self_kstack 的值作为栈顶，
	 * 此时线程内核栈栈顶 uint32_t* thread->self_kstack 指向线程栈的最低处，这是我们在函数 thread_create 中设定的。
	 * 接下来的 4 个弹栈操作，使之前初始化的 0 弹入到相应寄存器中。
	 * ret 会把栈顶的数据作为返回地址送上处理器的 EIP 寄存器。
	 * 此时栈顶的数据是什么？
	 *【去看线程栈结构体的定义】
	 * 弹完了4个寄存器之后，栈顶是在 eip 处
	 * 之前 thread_create 中为 kthread_stack->eip 赋值为 kernel_thread。
	 * 在执行 ret 后，esp 指向的数值被弹岀到 eip。
	 * 处理器会去执行 kernel_thread 函数。
	 * 在执行 kernel_thread 时，处理器以为栈顶是返回地址，其实只是一个假的占位置的变量 unused_retaddr！
	 * 接着会调用函数 function(func_arg) */
	//asm volatile ("movl %0, %%esp; pop %%ebp; pop %%ebx; pop %%edi; pop %%esi; ret" : : "g" (thread->self_kstack) : "memory");
	return thread;
}


/* 将kernel中的main函数完善为主线程 */
static void make_main_thread(void) {
	/* 因为main线程早已运行,咱们在loader.S中进入内核时的mov esp, 0xc009f000设置了栈顶，也预留了pcb, 地址为0xc009e000
	 * 主线程不需要再为PCB申请页，只需要通过 init_thread 填充PCB成员：名称和优先级等。
	 * 也不需要再通过 thread_create 构造它的线程栈。
	 */
	g_main_thread = running_thread();	//用栈指针的高20位，当做当前线程的PCB
	init_thread(g_main_thread, "main", 31);

	/* main函数是当前线程,当前线程不在thread_ready_list中,
	 * 所以只将其加在thread_all_list中. */
	ASSERT(!elem_find(&thread_all_list, &g_main_thread->all_list_tag));
	list_append(&thread_all_list, &g_main_thread->all_list_tag);
}

/*
完整的调度过程需要三部分的配合。
（1）时钟中断处理函数。（后续可能有block的情况调用schedule）
（2）调度器 schedule。
（3）任务切换函数 switch_to。
*/
/* 实现任务调度 */
void schedule()
{
	ASSERT(intr_get_status() == INTR_OFF);

	/* 换下 */
	struct task_struct* cur = running_thread(); 
	if (cur->status == TASK_RUNNING) { // 若此线程只是cpu时间片到了,将其加入到就绪队列尾
		ASSERT(!elem_find(&thread_ready_list, &cur->general_tag));
		list_append(&thread_ready_list, &cur->general_tag);
		cur->ticks = cur->priority;     // 重新将当前线程的ticks再重置为其priority;
		cur->status = TASK_READY;
	} else {
		/* 若此线程需要某事件发生后才能继续上cpu运行,
		不需要将其加入队列,因为当前线程不在就绪队列中。*/
	}

	/* 换上 */
	ASSERT(!list_empty(&thread_ready_list));
	g_thread_tag = NULL;	  // g_thread_tag清空
	/* 将thread_ready_list队列中的第一个就绪线程弹出,准备将其调度上cpu. */
	g_thread_tag = list_pop(&thread_ready_list);   
	struct task_struct* next = elem2entry(struct task_struct, general_tag, g_thread_tag);
	next->status = TASK_RUNNING;
	switch_to(cur, next);
}

/* 当前线程将自己阻塞,标志其状态为stat. */
void thread_block(enum task_status stat) {
	/* stat取值为TASK_BLOCKED,TASK_WAITING,TASK_HANGING,也就是只有这三种状态才不会被调度*/
	ASSERT(((stat == TASK_BLOCKED) || (stat == TASK_WAITING) || (stat == TASK_HANGING)));
	enum intr_status old_status = intr_disable();
	
	struct task_struct* cur_thread = running_thread();
	cur_thread->status = stat;
	schedule();
	
	/* 待当前线程被解除阻塞后才继续运行下面的intr_set_status */
	intr_set_status(old_status);
}

/* 将线程pthread解除阻塞 */
void thread_unblock(struct task_struct* pthread) {
	enum intr_status old_status = intr_disable();
	ASSERT(((pthread->status == TASK_BLOCKED) || (pthread->status == TASK_WAITING) || (pthread->status == TASK_HANGING)));
	
	if (pthread->status != TASK_READY) {
		ASSERT(!elem_find(&thread_ready_list, &pthread->general_tag));
		if (elem_find(&thread_ready_list, &pthread->general_tag)) {
			PANIC("thread_unblock: blocked thread in ready_list\n");
		}
		list_push(&thread_ready_list, &pthread->general_tag);	// 放到队列的最前面,使其尽快得到调度
		pthread->status = TASK_READY;
	}
	
	intr_set_status(old_status);
}

/* 初始化线程环境 */
void thread_init(void) {
	put_str("thread_init start\n");
	list_init(&thread_ready_list);
	list_init(&thread_all_list);
	/* 将当前main函数创建为线程 */
	make_main_thread();
	put_str("thread_init done\n");
}
