[bits 32]
section .text
global switch_to
; 这样被调用的 switch_to(cur, next); 两个参数是两个线程的PCB
; switch_to时的栈：
; next		<-- esp+24
; cur		<-- esp+20
; 返回地址	<-- esp+16
; esi		<-- esp+12
; edi		<-- esp+8
; ebx		<-- esp+4
; ebp 		<-- esp
switch_to:
   ;栈中此处是返回地址
   push esi
   push edi
   push ebx
   push ebp

   mov eax, [esp + 20]	; 得到栈中的参数 cur = [esp+20]
   mov [eax], esp		; 保存pcb第一个成员：栈顶指针esp，即task_struct(pcb)的self_kstack字段
;------------------  以上是备份当前线程的环境，下面是恢复下一个线程的环境  ----------------
; 在同一次 switch_to 的调用执行中，
; 之前保存的寄存器属于当前线程 cur
; 之后恢复的寄存器属于下一个线程 next
;------------------------------------------------------------------------------------------
   mov eax, [esp + 24]	; 得到栈中的参数 next = [esp+24]
   mov esp, [eax]		; pcb的第一个成员是self_kstack字段，它就是0级栈顶指针
						; 用来上cpu时恢复0级栈，0级栈中保存了进程或线程所有信息,包括3级栈指针
   pop ebp
   pop ebx
   pop edi
   pop esi
   ret					; 返回到上面switch_to第一行那句注释的返回地址,
						; 1.如果线程是第一次执行，则会返回到kernel_thread，是在thread_create中设置的
						; 2.如果线程执行过了，则会返回到schedule函数，再返回到intr_timer_handler函数
						; 再返回到kernel.S中的jmp intr_exit 从而恢复任务的全部寄存器映像，
						; 之后通过 iretd 指令退出中断，任务被完全彻底地恢复。
