#ifndef __USERPROG_PROCESS_H 
#define __USERPROG_PROCESS_H 
#include "thread.h"
#include "stdint.h"
#define default_prio 31
/*
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
|                      | --栈顶 0xc0000_0000 - 0x1000
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
#define USER_STACK3_VADDR  (0xc0000000 - 0x1000)

/* 0x8048000 是linux用户程序入口地址
 * 可执行程序的 Entry point address 都是在 0x8048000 附近*/
#define USER_VADDR_START 0x8048000

void process_execute(void* filename, char* name);
void start_process(void* filename_);
void process_activate(struct task_struct* p_thread);
void page_dir_activate(struct task_struct* p_thread);
uint32_t* create_page_dir(void);
void create_user_vaddr_bitmap(struct task_struct* user_prog);
#endif
