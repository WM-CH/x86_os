#include "print.h"
#include "init.h"
#include "thread.h"
#include "interrupt.h"
#include "console.h"
#include "process.h"
#include "syscall-init.h"
#include "syscall.h"
#include "stdio.h"

void k_thread_a(void*);
void k_thread_b(void*);
void u_prog_a(void);
void u_prog_b(void);

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
int main(void) {
   put_str("I am kernel\n");
   init_all();

   process_execute(u_prog_a, "u_prog_a");
   process_execute(u_prog_b, "u_prog_b");

   console_put_str(" I am main, my pid:0x");
   console_put_int(sys_getpid());
   console_put_char('\n');
   intr_enable();
   thread_start("k_thread_a", 31, k_thread_a, "I am thread_a");
   thread_start("k_thread_b", 31, k_thread_b, "I am thread_b ");
   while(1);
   return 0;
}

/* 在线程中运行的函数 */
void k_thread_a(void* arg) {     
   char* para = arg;
   console_put_str(" I am thread_a, my pid:0x");
   console_put_int(sys_getpid());
   console_put_char('\n');
   while(1);
}

/* 在线程中运行的函数 */
void k_thread_b(void* arg) {     
   char* para = arg;
   console_put_str(" I am thread_b, my pid:0x");
   console_put_int(sys_getpid());
   console_put_char('\n');
   while(1);
}

/* 测试用户进程 */
void u_prog_a(void) {
   char* name = "prog_a";
   printf(" I am %s, my pid:%d%c", name, getpid(),'\n');
   while(1);
}

/* 测试用户进程 */
void u_prog_b(void) {
   char* name = "prog_b";
   printf(" I am %s, my pid:%d%c", name, getpid(), '\n');
   while(1);
}



