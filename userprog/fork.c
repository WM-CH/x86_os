#include "fork.h"
#include "process.h"
#include "memory.h"
#include "interrupt.h"
#include "debug.h"
#include "thread.h"    
#include "string.h"
#include "file.h"

extern void intr_exit(void);

/* 将父进程的pcb、虚拟地址位图拷贝给子进程 */
static int32_t copy_pcb_vaddrbitmap_stack0(struct task_struct* child_thread, struct task_struct* parent_thread) {
	/* a 复制pcb所在的整个页,里面包含进程pcb信息 + 0特权级的栈
	栈里面包含了返回地址
	然后再单独修改个别部分 */
	memcpy(child_thread, parent_thread, PG_SIZE);
	// 初始化PCB部分
	child_thread->pid = fork_pid();
	child_thread->elapsed_ticks = 0;
	child_thread->status = TASK_READY;
	child_thread->ticks = child_thread->priority;   // 为新进程把时间片充满
	child_thread->parent_pid = parent_thread->pid;
	child_thread->general_tag.prev = child_thread->general_tag.next = NULL;
	child_thread->all_list_tag.prev = child_thread->all_list_tag.next = NULL;
	// 子进程内存块（堆内存）
	block_desc_init(child_thread->u_block_desc);
	/* b 复制父进程的虚拟地址池的位图  可执行程序入口地址：USER_VADDR_START==0x8048000 */
	uint32_t bitmap_pg_cnt = DIV_ROUND_UP((0xc0000000 - USER_VADDR_START) / PG_SIZE / 8 , PG_SIZE);
	void* vaddr_btmp = get_kernel_pages(bitmap_pg_cnt);
	if (vaddr_btmp == NULL)
		return -1;
	/* 此时child_thread->userprog_vaddr.vaddr_bitmap.bits还是指向父进程虚拟地址的位图地址
	 * 下面将child_thread->userprog_vaddr.vaddr_bitmap.bits指向自己的位图vaddr_btmp */
	memcpy(vaddr_btmp, child_thread->userprog_vaddr.vaddr_bitmap.bits, bitmap_pg_cnt * PG_SIZE);	//复制父进程的虚拟地址位图
	child_thread->userprog_vaddr.vaddr_bitmap.bits = vaddr_btmp;	//指向子进程自己的了
	/* 调试用 */
	ASSERT(strlen(child_thread->name) < 11);	// pcb.name的长度是16,为避免下面strcat越界
	strcat(child_thread->name,"_fork");
	return 0;
}

/* 复制子进程的进程体(代码和数据)及用户栈 */
/*
用户进程使用的内存是用虚拟内存池来管理的，也就是 pcb 中的 userprog_vaddr

用户进程：【这里只是用户进程的fork，不适用于内核线程的fork】
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
*/
static void copy_body_stack3(struct task_struct* child_thread, struct task_struct* parent_thread, void* buf_page) {
	uint8_t* vaddr_btmp = parent_thread->userprog_vaddr.vaddr_bitmap.bits;
	uint32_t btmp_bytes_len = parent_thread->userprog_vaddr.vaddr_bitmap.btmp_bytes_len;
	uint32_t vaddr_start = parent_thread->userprog_vaddr.vaddr_start;
	uint32_t idx_byte = 0;
	uint32_t idx_bit = 0;
	uint32_t prog_vaddr = 0;

	/* 在父进程的用户空间中查找已有数据的页，按页一一拷贝给子进程 */
	while (idx_byte < btmp_bytes_len) {
		if (vaddr_btmp[idx_byte]) {
			idx_bit = 0;
			while (idx_bit < 8) {
				if ((BITMAP_MASK << idx_bit) & vaddr_btmp[idx_byte]) {	//BITMAP_MASK=1
					prog_vaddr = (idx_byte * 8 + idx_bit) * PG_SIZE + vaddr_start;
					/* 下面的操作是将父进程用户空间中的数据通过内核空间做中转,最终复制到子进程的用户空间 */

					/* a 将父进程在用户空间中的数据复制到"内核缓冲区buf_page"
					目的：下面切换到子进程的页表后,还能访问到父进程的数据*/
					memcpy(buf_page, (void*)prog_vaddr, PG_SIZE);

					/* b 将页表切换到子进程，下面申请内存的函数，将pte及pde安装在子进程的页表中 */
					page_dir_activate(child_thread);
					/* c 申请虚拟地址prog_vaddr */
					get_a_page_without_opvaddrbitmap(PF_USER, prog_vaddr);	//位图已经拷贝过了，当然不用再操作了...

					/* d 从内核缓冲区中将父进程数据复制到子进程的用户空间 */
					memcpy((void*)prog_vaddr, buf_page, PG_SIZE);

					/* e 恢复父进程页表 */
					page_dir_activate(parent_thread);
				}
				idx_bit++;
			}
		}
		idx_byte++;
	}
}

/* 为子进程构建 thread_stack 和修改返回值 */
/* 0级栈、中断栈：
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

	// 以下由cpu从低特权级进入高特权级时，CPU自动压入
	uint32_t err_code;		// err_code会被压入在eip之后
	void (*eip) (void);
	uint32_t cs;
	uint32_t eflags;
	void* esp;
	uint32_t ss;			// 因为要切换到高特权级栈，所以旧栈ss:esp要压入【第一个压入】
};

***********  线程栈 thread_stack  ***********
* 线程自己的栈
* 1.用于存储线程中待执行的函数
* 此结构在线程自己的内核栈中位置不固定,
* 2.用在switch_to时保存线程环境。
* 实际位置取决于实际运行情况。
******************************************
struct thread_stack {
	* ABI规定 ebp/ebx/edi/esi/esp 归主调函数所用
	* 被调函数执行完后，这5个寄存器不允许被改变  
	* 在 switch_to 中，一上来先保存这几个寄存器
	uint32_t ebp;				// 【低地址处】【最后压入】
	uint32_t ebx;
	uint32_t edi;
	uint32_t esi;

	* 线程第一次执行时 eip 指向待调用的函数 kernel_thread
	* 其它时候 eip 指向 switch_to 任务切换后新任务的返回地址
	void (*eip) (thread_func* func, void* func_arg);

	* 以下仅供第一次被调度上cpu时使用【这里没赋值这些】
	void (*unused_retaddr);	// unused_ret只为占位置充数，为“返回地址”保留位置
	thread_func* function;	// 由 kernel_thread 函数调用的函数名
	void* func_arg;			// 由 kernel_thread 函数调用 function 时的参数【高地址处】
};

父进程在执行 fork 系统调用时会进入内核态，中断入口程序会保存父进程的上下文，
其中包括进程在用户态下的 CS:EIP 的值，因此父进程从 fork 系统调用返回后，可以继续 fork 之后的代码执行。
子进程也是从 fork 后的代码处继续运行的，这是怎样做到的呢？
在这之前我们已经通过函数 copy_pcb_vaddrbitmap_stack0 将父进程的内核栈复制到了子进程的内核栈中，那里保存了返回地址，也就是 fork 之后的地址，
为了让子进程也能继续 fork 之后的代码运行，必须让它同父进程一样，从中断退出，也就是要经过 intr_exit。

子进程是由调度器 schedule 调度执行的，它要用到 switch_to 函数，
而 switch_to 函数要从栈 thread_stack 中恢复上下文，因此我们要想办法构建出合适的 thread_stack
*/
static int32_t build_child_stack(struct task_struct* child_thread) {
	/* a 使子进程pid返回值为0 */
	/* 获取子进程0级栈栈顶 */
	struct intr_stack* intr_0_stack = (struct intr_stack*)((uint32_t)child_thread + PG_SIZE - sizeof(struct intr_stack));
	/* 修改子进程的返回值为0 */
	intr_0_stack->eax = 0;	//根据 abi 约定，eax 寄存器中是函数返回值!!!

	/* b 为 switch_to 构建线程栈 struct thread_stack,将其构建在紧临intr_stack之下的空间*/
	uint32_t* ret_addr_in_thread_stack  = (uint32_t*)intr_0_stack - 1;	// 任务切换后新任务的返回地址 eip

	/***   这三行不是必要的,只是为了梳理 thread_stack 中的关系 ***/
	uint32_t* esi_ptr_in_thread_stack = (uint32_t*)intr_0_stack - 2;
	uint32_t* edi_ptr_in_thread_stack = (uint32_t*)intr_0_stack - 3;
	uint32_t* ebx_ptr_in_thread_stack = (uint32_t*)intr_0_stack - 4;
	/**********************************************************/

	/* ebp 在 thread_stack 中的地址便是当时的esp(0级栈的栈顶)，即栈顶esp = (uint32_t*)intr_0_stack - 5 */
	/* 指针 ebp_ptr_in_thread_stack，它是 thread_stack 的栈顶，
	 * 必须把它的值存放在 pcb 中偏移为 0 的地方，即 task_struct 中的 self_kstack 处，
	 * 将来 switch_to 要用它作为栈顶，并且执行一系列的 pop 来恢复上下文。 */
	uint32_t* ebp_ptr_in_thread_stack = (uint32_t*)intr_0_stack - 5;

	/* switch_to的返回地址更新为intr_exit，直接从中断返回 */
	*ret_addr_in_thread_stack = (uint32_t)intr_exit;	// kernel.S

	/* 下面这个赋值只是为了使构建的 thread_stack 更加清晰,其实也不需要,
	 * 因为在进入intr_exit后一系列的pop会把寄存器中的数据覆盖 */
	*ebp_ptr_in_thread_stack = *ebx_ptr_in_thread_stack = *edi_ptr_in_thread_stack = *esi_ptr_in_thread_stack = 0;
	/*********************************************************/

	/* 把构建的 thread_stack 的栈顶做为 switch_to 恢复数据时的栈顶 */
	child_thread->self_kstack = ebp_ptr_in_thread_stack;	//线程内核栈栈顶 self_kstack 总是和 esp 来回赋值~
	/*
	在被换下处理器前，我们会把线程的上下文信息保存在 0 特权级栈中，
	self_kstack 便用来记录 0 特权级栈在保存线程上下文后，新的栈顶，
	在下一次此线程又被调度到处理器上时，
	把 self_kstack 的值加载到 esp 寄存器，这样便从 0 特权级栈中获取了线程上下文，从而可以加载到处理器中运行。
	*/
	return 0;
}

/* 更新inode打开数 */
static void update_inode_open_cnts(struct task_struct* thread) {
	int32_t local_fd = 3, global_fd = 0;
	while (local_fd < MAX_FILES_OPEN_PER_PROC) {	//遍历线程打开的所有文件
		global_fd = thread->fd_table[local_fd];
		ASSERT(global_fd < MAX_FILE_OPEN);
		if (global_fd != -1) {
			file_table[global_fd].fd_inode->i_open_cnts++;	//线程打开的所有文件的inode->open_cnt加一
		}
		local_fd++;
	}
}

/* 拷贝父进程本身所占资源给子进程 */
static int32_t copy_process(struct task_struct* child_thread, struct task_struct* parent_thread) {
	/* 内核缓冲区,作为父进程用户空间的数据，复制到子进程用户空间，的中转 */
	void* buf_page = get_kernel_pages(1);
	if (buf_page == NULL) {
		return -1;
	}

	/* a 复制父进程的pcb、虚拟地址位图、内核栈到子进程 */
	if (copy_pcb_vaddrbitmap_stack0(child_thread, parent_thread) == -1) {
		return -1;
	}

	/* b 为子进程创建页表,此页表仅包括内核空间 */
	child_thread->pgdir = create_page_dir();
	if(child_thread->pgdir == NULL) {
		return -1;
	}

	/* c 复制父进程进程体及用户栈给子进程 */
	copy_body_stack3(child_thread, parent_thread, buf_page);

	/* d 构建子进程 thread_stack 和修改返回值pid */
	build_child_stack(child_thread);

	/* e 更新文件inode的打开数 */
	update_inode_open_cnts(child_thread);

	mfree_page(PF_KERNEL, buf_page, 1);
	return 0;
}

/* fork子进程，内核线程不可直接调用 */
pid_t sys_fork(void) {
	struct task_struct* parent_thread = running_thread();
	struct task_struct* child_thread = get_kernel_pages(1);	// 为子进程创建pcb(task_struct结构)
	if (child_thread == NULL) {
		return -1;
	}
	ASSERT(INTR_OFF == intr_get_status() && parent_thread->pgdir != NULL);	//中断关了，并且是用户线程

	if (copy_process(child_thread, parent_thread) == -1) {
		return -1;
	}

	/* 添加到就绪线程队列和所有线程队列，子进程由调度器安排运行 */
	ASSERT(!elem_find(&thread_ready_list, &child_thread->general_tag));
	list_append(&thread_ready_list, &child_thread->general_tag);
	ASSERT(!elem_find(&thread_all_list, &child_thread->all_list_tag));
	list_append(&thread_all_list, &child_thread->all_list_tag);

	return child_thread->pid;	// 父进程返回子进程的pid
}

