#include "wait_exit.h"
#include "global.h"
#include "debug.h"
#include "thread.h"
#include "list.h"
#include "stdio-kernel.h"
#include "memory.h"
#include "bitmap.h"
#include "fs.h"
#include "file.h"
#include "pipe.h"

/* 释放用户进程资源: 
 * 1 页表中对应的物理页
 * 2 虚拟内存池占物理页框
 * 3 关闭打开的文件 */
static void release_prog_resource(struct task_struct* release_thread) {
	uint32_t* pgdir_vaddr = release_thread->pgdir;	// 进程自己页目录表的虚拟地址，加载到cr3时需转成物理地址
	// 前768个页目录项，表示用户空间的低端3G虚拟内存。
	// 剩下1024-768=256个页目录项，是内核使用的高端1G虚拟内存。
	uint16_t user_pde_nr = 768, pde_idx = 0;
	uint32_t pde = 0;
	uint32_t* v_pde_ptr = NULL;		// v表示var,和函数pde_ptr区分

	uint16_t user_pte_nr = 1024, pte_idx = 0;
	uint32_t pte = 0;
	uint32_t* v_pte_ptr = NULL;		// 加个v表示var,和函数pte_ptr区分

	uint32_t* first_pte_vaddr_in_pde = NULL;	// 用来记录pde指向的页表中的，第0个pte的地址
	uint32_t pg_phy_addr = 0;

	/* 回收页表中用户空间的页框 */
	/* 回收的方法有两种，一是按照虚拟内存池 pcb->userprog_vaddr 回收，检查位图中被置为 1 的 bit，计算出相应虚拟地址，逐位回收。
	另一种方法相对直接一点，直接遍历页表，如果页表的 p 位为 1，这说明已经分配了物理页框。
	第 1 种方法咱们已经在实现 fork 时为复制用户地址空间用过了，
	现在咱们尝试第二种方法。*/
	while (pde_idx < user_pde_nr) {
		v_pde_ptr = pgdir_vaddr + pde_idx;	// pgdir_vaddr 类型是 u32* 指针步长是4字节，一个页目录项的大小
		pde = *v_pde_ptr;			// 【取到页目录表中的页目录项，包含所指向的页表地址+属性】
		if (pde & 0x00000001) {		// 如果页目录项p位为1,表示该页目录项下可能有页表项
			/* pte_ptr功能：得到虚拟地址vaddr对应的pte指针*/
			/* 取虚拟地址pde_idx*0x400000的pte指针，就限制了，取到的都是，所有页表中的第0个页表项的指针
			 * 因为限定了跨度是 一个页表能表示的虚拟地址范围。
			 */
			first_pte_vaddr_in_pde = pte_ptr(pde_idx * 0x400000);	// 一个pde指向的页表，表示的内存容量是4M(4k页大小*1024个页),即0x400000
			pte_idx = 0;
			while (pte_idx < user_pte_nr) {
				v_pte_ptr = first_pte_vaddr_in_pde + pte_idx;	// first_pte_vaddr_in_pde 类型是 u32* 指针步长是4字节，一个页表项的大小
				pte = *v_pte_ptr;	// 【取到页表中的页表项，包含所指向的页地址+属性】
				if (pte & 0x00000001) {
					/* 将pte中记录的物理页框直接在相应内存池的位图中清0 */
					pg_phy_addr = pte & 0xfffff000;	// 【页地址】
					free_a_phy_page(pg_phy_addr);
				}
				pte_idx++;
			}
			/* 将pde中记录的物理页框直接在相应内存池的位图中清0 */
			pg_phy_addr = pde & 0xfffff000;
			free_a_phy_page(pg_phy_addr);
		}
		pde_idx++;
	}

	/* 回收用户虚拟地址池所占的物理内存*/
	uint32_t bitmap_pg_cnt = (release_thread->userprog_vaddr.vaddr_bitmap.btmp_bytes_len) / PG_SIZE;
	uint8_t* user_vaddr_pool_bitmap = release_thread->userprog_vaddr.vaddr_bitmap.bits;
	mfree_page(PF_KERNEL, user_vaddr_pool_bitmap, bitmap_pg_cnt);

	/* 关闭进程打开的文件 */
	uint8_t local_fd = 3;
	while(local_fd < MAX_FILES_OPEN_PER_PROC) {
		if (release_thread->fd_table[local_fd] != -1) {
			if (is_pipe(local_fd)) {
				uint32_t global_fd = fd_local2global(local_fd);
				if (--file_table[global_fd].fd_pos == 0) {
					mfree_page(PF_KERNEL, file_table[global_fd].fd_inode, 1);
					file_table[global_fd].fd_inode = NULL;
				}
			} else {
				sys_close(local_fd);
			}
		}
		local_fd++;
	}
}

/* list_traversal的回调函数：
 * 查找pelem的parent_pid是否是ppid,成功返回true,失败则返回false */
static bool find_child(struct list_elem* pelem, int32_t ppid) {
	/* elem2entry中间的参数all_list_tag取决于pelem对应的变量名 */
	struct task_struct* pthread = elem2entry(struct task_struct, all_list_tag, pelem);
	if (pthread->parent_pid == ppid) {	// 若该任务的parent_pid为ppid,返回
		return true;	// list_traversal只有在回调函数返回true时才会停止继续遍历,所以在此返回true
	}
	return false;		// 让list_traversal继续传递下一个元素
}

/* list_traversal的回调函数：
 * 查找状态为TASK_HANGING的任务 */
static bool find_hanging_child(struct list_elem* pelem, int32_t ppid) {
	struct task_struct* pthread = elem2entry(struct task_struct, all_list_tag, pelem);
	if (pthread->parent_pid == ppid && pthread->status == TASK_HANGING) {
		return true;
	}
	return false; 
}

/* list_traversal的回调函数：
 * 将一个子进程过继给init */
static bool init_adopt_a_child(struct list_elem* pelem, int32_t pid) {
	struct task_struct* pthread = elem2entry(struct task_struct, all_list_tag, pelem);
	if (pthread->parent_pid == pid) {	// 若该进程的parent_pid为pid,返回
		pthread->parent_pid = 1;		// init进程pid是1
	}
	return false;	// 让list_traversal继续传递下一个元素
}

/* 父进程等待子进程调用exit,将子进程的退出状态保存到status指向的变量.
 * 成功则返回子进程的pid,失败则返回-1 */
pid_t sys_wait(int32_t* status) {
	struct task_struct* parent_thread = running_thread();

	while(1) {
		/* 优先处理已经是挂起hanging状态的任务 */
		struct list_elem* child_elem = list_traversal(&thread_all_list, find_hanging_child, parent_thread->pid);
		/* 若有挂起的子进程 */
		if (child_elem != NULL) {
			struct task_struct* child_thread = elem2entry(struct task_struct, all_list_tag, child_elem);
			*status = child_thread->exit_status;

			/* thread_exit之后,pcb会被回收,因此提前获取pid */
			uint16_t child_pid = child_thread->pid;

			/* 2 从就绪队列和全部队列中删除进程表项*/
			thread_exit(child_thread, false);	// 传入的参数need_schedule=false,使thread_exit调用后回到此处
			/* 进程表项是进程或线程的最后保留的资源, 至此该进程彻底消失了 */

			return child_pid;
		}

		/* 判断是否有子进程 */
		child_elem = list_traversal(&thread_all_list, find_child, parent_thread->pid);
		if (child_elem == NULL) {	// 若没有子进程则出错返回
			return -1;
		} else {
			/* 若子进程还未运行完,即还未调用exit,则将自己挂起,直到子进程在执行exit时将自己唤醒 */
			thread_block(TASK_WAITING); 
		}
	}
}

/* 子进程用来结束自己时调用 */
void sys_exit(int32_t status) {
	struct task_struct* child_thread = running_thread();
	child_thread->exit_status = status;		//返回值
	if (child_thread->parent_pid == -1) {
		PANIC("sys_exit: child_thread->parent_pid is -1\n");
	}

	/* 将进程child_thread的所有子进程都过继给init */
	list_traversal(&thread_all_list, init_adopt_a_child, child_thread->pid);

	/* 回收进程child_thread的资源 */
	release_prog_resource(child_thread); 

	/* 如果父进程正在等待子进程退出,将父进程唤醒 */
	struct task_struct* parent_thread = pid2thread(child_thread->parent_pid);
	if (parent_thread->status == TASK_WAITING) {
		thread_unblock(parent_thread);
	}

	/* 将自己挂起,等待父进程获取其status,并回收其pcb */
	thread_block(TASK_HANGING);
}
