#include "print.h"
#include "init.h"
#include "thread.h"
#include "interrupt.h"
#include "console.h"

void k_thread_a(void*);
void k_thread_b(void*);
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
信号量、锁

*/
int main(void) {
	put_str("I am kernel\n");
	init_all();
	
	// kernel线程优先级也是31 所以打印个数"Main"和"argA"相同，是"argB"的4倍
	thread_start("k_thread_a", 31, k_thread_a, "argA ");
	thread_start("k_thread_b", 8, k_thread_b, "argB ");

	intr_enable();	// 打开中断,使时钟中断起作用
	while(1) {
		console_put_str("Main ");
	};
	return 0;
}

/* 在线程中运行的函数 */
void k_thread_a(void* arg) {
	char* para = arg;
	while(1) {
		console_put_str(para);
	}
}

/* 在线程中运行的函数 */
void k_thread_b(void* arg) {
	char* para = arg;
	while(1) {
		console_put_str(para);
	}
}





