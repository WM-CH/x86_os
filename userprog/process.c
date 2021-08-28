#include "process.h"
#include "global.h"
#include "debug.h"
#include "memory.h"
#include "thread.h"    
#include "list.h"    
#include "tss.h"    
#include "interrupt.h"
#include "string.h"
#include "console.h"

extern void intr_exit(void);

/* 构建用户进程初始上下文信息
 * 填充用户进程的中断栈，并将中断栈的信息，利用iretd填充到 CPU 中
 */
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

/* 更新CR3寄存器，激活页目录表 */
void page_dir_activate(struct task_struct* p_thread) {
	
	/* 若为"内核线程",需要重新填充页表为0x100000 */
	uint32_t pagedir_phy_addr = 0x100000;	// 默认为内核的页目录物理地址,也就是内核线程所用的页目录表
	
	/* "用户态进程"有自己的页目录表 */
	if (p_thread->pgdir != NULL) {
		pagedir_phy_addr = addr_v2p((uint32_t)p_thread->pgdir);
	}

	/* 更新页目录寄存器cr3,使新页表生效
	 * 实现进程间，虚拟地址空间的隔离。
	 */
	asm volatile ("movl %0, %%cr3" : : "r" (pagedir_phy_addr) : "memory");
}

/* 1.激活线程或进程的页表,【更新CR3寄存器】
 * 2.用户进程，更新tss中的esp0为进程的特权级0的栈
 * 【只有在任务调度时（中断时），才会切换页表及更新 0 级栈，因此 process_activate 是被"中断执行流"的 schedule 调用的！】
 */
void process_activate(struct task_struct* p_thread) {
	ASSERT(p_thread != NULL);
	
	/* 激活该进程或线程的页目录表【重新加载了CR3】 */
	page_dir_activate(p_thread);

	/* 内核线程特权级本身就是0,处理器进入中断时并不会从tss中获取0特权级栈地址,故不需要更新esp0 */
	if (p_thread->pgdir != NULL) {
		/* 若为用户进程,更新该进程的esp0,用于此进程被中断时保留上下文 
		 * 需要从tss中获取0特权级栈地址 */
		update_tss_esp(p_thread);	//就一行：g_tss.esp0 = (uint32_t*)((uint32_t)pthread + PG_SIZE);
	}
}

/* 创建页目录表
 * 成功则返回页目录的虚拟地址,否则返回-1 
 * 
 * 要确保用户进程，在自己的地址空间中，能够访问到内核。
 * 即：任意进程的页目录表中，用户进程占第 0~767 个页目录项，内核占第 768~1023 个页目录项。
 * 0x300 = 768
 */
uint32_t* create_page_dir(void) {

	/* 用户进程的页表不能让用户直接访问到,所以在内核空间来申请 */
	uint32_t* page_dir_vaddr = get_kernel_pages(1);
	if (page_dir_vaddr == NULL) {
		console_put_str("create_page_dir: get_kernel_page failed!");
		return NULL;
	}

	/*** 1 复制内核的页目录表  ***/
	// page_dir_vaddr + 0x300*4 是内核页目录的第768项
	// 复制的页目录项个数=1024/4
	memcpy((uint32_t*)((uint32_t)page_dir_vaddr + 0x300*4), (uint32_t*)(0xfffff000+0x300*4), 1024);

	/*** 2 更新页目录表地址 ***/
	// 页目录表地址是页目录的最后一项
	uint32_t new_page_dir_phy_addr = addr_v2p((uint32_t)page_dir_vaddr);
	page_dir_vaddr[1023] = new_page_dir_phy_addr | PG_US_U | PG_RW_W | PG_P_1;

	return page_dir_vaddr;
}

/* 创建用户进程虚拟地址位图 */
void create_user_vaddr_bitmap(struct task_struct* user_prog) {
   user_prog->userprog_vaddr.vaddr_start = USER_VADDR_START;	// linux用户程序入口地址 0x80480000
   uint32_t bitmap_pg_cnt = DIV_ROUND_UP((0xc0000000 - USER_VADDR_START) / PG_SIZE / 8 , PG_SIZE);
   user_prog->userprog_vaddr.vaddr_bitmap.bits = get_kernel_pages(bitmap_pg_cnt);
   user_prog->userprog_vaddr.vaddr_bitmap.btmp_bytes_len = (0xc0000000 - USER_VADDR_START) / PG_SIZE / 8;
   bitmap_init(&user_prog->userprog_vaddr.vaddr_bitmap);
}

/* 创建用户进程 */
void process_execute(void* filename, char* name) {
   /* pcb内核的数据结构,由内核来维护进程信息,因此要在内核内存池中申请 */
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

