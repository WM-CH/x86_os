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
void k_thread_a(void*);
void k_thread_b(void*);
void u_prog_a(void);
void u_prog_b(void);

int main(void) {
	put_str("I am kernel\n");
	init_all();
	process_execute(u_prog_a, "u_prog_a");
	process_execute(u_prog_b, "u_prog_b");
	thread_start("k_thread_a", 31, k_thread_a, "I am thread_a");
	thread_start("k_thread_b", 31, k_thread_b, "I am thread_b");
	sys_open("/file1", O_CREAT);
	while(1);
	return 0;
}

/* 在线程中运行的函数 */
void k_thread_a(void* arg) {     
	void* addr1 = sys_malloc(256);
	void* addr2 = sys_malloc(255);
	void* addr3 = sys_malloc(254);
	console_put_str(" thread_a malloc addr:0x");
	console_put_int((int)addr1);
	console_put_char(',');
	console_put_int((int)addr2);
	console_put_char(',');
	console_put_int((int)addr3);
	console_put_char('\n');

	int cpu_delay = 100000;
	while(cpu_delay-- > 0);
	sys_free(addr1);
	sys_free(addr2);
	sys_free(addr3);
	while(1);
}

/* 在线程中运行的函数 */
void k_thread_b(void* arg) {     
	void* addr1 = sys_malloc(256);
	void* addr2 = sys_malloc(255);
	void* addr3 = sys_malloc(254);
	console_put_str(" thread_b malloc addr:0x");
	console_put_int((int)addr1);
	console_put_char(',');
	console_put_int((int)addr2);
	console_put_char(',');
	console_put_int((int)addr3);
	console_put_char('\n');

	int cpu_delay = 100000;
	while(cpu_delay-- > 0);
	sys_free(addr1);
	sys_free(addr2);
	sys_free(addr3);
	while(1);
}

/* 测试用户进程 */
void u_prog_a(void) {
	void* addr1 = malloc(256);
	void* addr2 = malloc(255);
	void* addr3 = malloc(254);
	printf(" prog_a malloc addr:0x%x,0x%x,0x%x\n", (int)addr1, (int)addr2, (int)addr3);

	int cpu_delay = 100000;
	while(cpu_delay-- > 0);
	free(addr1);
	free(addr2);
	free(addr3);
	while(1);
}

/* 测试用户进程 */
void u_prog_b(void) {
	void* addr1 = malloc(256);
	void* addr2 = malloc(255);
	void* addr3 = malloc(254);
	printf(" prog_b malloc addr:0x%x,0x%x,0x%x\n", (int)addr1, (int)addr2, (int)addr3);

	int cpu_delay = 100000;
	while(cpu_delay-- > 0);
	free(addr1);
	free(addr2);
	free(addr3);
	while(1);
}

