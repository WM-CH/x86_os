#include "print.h"
#include "init.h"
#include "thread.h"
#include "interrupt.h"
#include "console.h"
#include "process.h"
#include "syscall-init.h"
#include "syscall.h"
#include "stdio.h"
#include "memory.h"
#include "fs.h"
#include "string.h"
#include "dir.h"
#include "assert.h"
#include "shell.h"
#include "ide.h"
#include "stdio-kernel.h"

/*
（1）上下文保护的第一部分，保存任务进入中断前的全部寄存器，目的是能让任务恢复到中断前。【kernel.S】
（2）上下文保护的第二部分，保存 esi、 edi、 ebx 和 ebp，目的是让任务恢复执行
在任务切换发生时剩下尚未执行的内核代码，【switch.S】
保证顺利走到退出中断的出口，
利用第一部分保护的寄存器环境彻底恢复任务。
*/
/*
1.理解thread.c中 thread_start 中 ret，它实现了线程切换
2.理解switch.c中 线程的切换
*/
/*
用户进程
u_prog_a 的地址是在 0xc0000000 以上，位于内核空间，但这并不表示它无法模拟用户进程。
*/
void init(void);

int main(void) {
	put_str("I am kernel\n");
	init_all();

	/*************    写入应用程序    *************/
	// 就第一次写入到hd80M.img就行，之后注释掉这块代码。
	uint32_t file_size = 4777;
	uint32_t sec_cnt = DIV_ROUND_UP(file_size, 512);
	struct disk* sda = &channels[0].devices[0];
	void* prog_buf = sys_malloc(file_size);
	ide_read(sda, 300, prog_buf, sec_cnt);
	int32_t fd = sys_open("/prog_no_arg", O_CREAT|O_RDWR);
	if (fd != -1) {
		if(sys_write(fd, prog_buf, file_size) == -1) {
			printk("file write error!\n");
			while(1);
		}
	}
	/*************    写入应用程序结束   *************/
	cls_screen();
	console_put_str("[rabbit@localhost /]$ ");
	while(1);
	return 0;
}

/* init进程 */
void init(void) {
   uint32_t ret_pid = fork();
   if(ret_pid) {  // 父进程
      while(1);
   } else {	  // 子进程
      my_shell();
   }
   panic("init: should not be here");
}

