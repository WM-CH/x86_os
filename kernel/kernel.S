[bits 32]
;着重看 7.4.1 中断时的保护
;下边是 7.4.2 中断时的压栈
; 1）如果发生了特权级转移，比如被中断的进程是 3 特权级，中断处理程序是 0 特权级，
;此时要把低特权级的栈段选择子 ss 及栈指针 esp 保存到栈中。
; 2）压入标志寄存器 eflags
; 3）压入返回地址，先压入 cs，后压入 eip
; 4）如果此中断没有相应的错误码，至此，处理器把寄存器压栈的工作完成.
;    如果此中断有错误码的话，处理器在压入 eip 之后，会压入错误码
; 5）如果栈中有错误码，在 iret 指令执行前必须要把栈中的错误码跨过！！！
; 此后开始使用 0 特权级的栈（ss/esp）
%define ERROR_CODE nop	; cpu已经自动压入了错误码,这里不做操作.
%define ZERO push 0		; cpu没有压入错误码,为了统一栈中格式,就手工压入一个0

extern idt_table		;idt_table是C中注册的中断处理程序数组

section .data
global intr_entry_table	; 数组名
intr_entry_table:

;宏 VECTOR 接受两个参数，使用时用%1/%2表示
%macro VECTOR 2
section .text
intr%1entry:		; 每个中断处理程序都要压入中断向量号（第一个参数%1）；intr%1entry在宏参数替换之后，就是个标号，标号就是个地址
	%2				; 会被展开成 nop，或者是 push 0【错误码的统一处理】
	; 以下是保存上下文环境
	push ds
	push es
	push fs
	push gs
	pushad			; PUSHAD指令压入32位寄存器,其入栈顺序是: EAX,ECX,EDX,EBX,ESP,EBP,ESI,EDI

	; 如果是从片上进入的中断,除了往从片上发送EOI外,还要往主片上发送EOI 
	mov al, 0x20		; 中断结束命令EOI
	out 0xa0, al		; 向从片发送
	out 0x20, al		; 向主片发送

	push %1			; 不管idt_table中的目标程序是否需要参数,都一律压入中断向量号,调试时很方便
	call [idt_table + %1*4]	; 调用idt_table中的C版本中断处理函数
	jmp intr_exit

section .data
	dd    intr%1entry		; 存储各个中断入口程序的地址，形成intr_entry_table数组
%endmacro

section .text
global intr_exit
intr_exit:	     
	; 以下是恢复上下文环境
	add esp, 4				; 跳过我们手动压入的中断号push %1
	popad
	pop gs
	pop fs
	pop es
	pop ds
	add esp, 4				; 跳过error_code，硬件自己压入的
	iretd

;使用宏 VECTOR 定义33个中断处理函数
;第一个参数是中断向量，第二个参数（也是宏）是中断处理函数要传入的参数
VECTOR 0x00,ZERO			;divide err
VECTOR 0x01,ZERO			;debug
VECTOR 0x02,ZERO			;NMI
VECTOR 0x03,ZERO 			;int3断点
VECTOR 0x04,ZERO			;into
VECTOR 0x05,ZERO			;bound range exceeded
VECTOR 0x06,ZERO			;invalid opcode
VECTOR 0x07,ZERO			;device not avaliable
VECTOR 0x08,ERROR_CODE		;double fault
VECTOR 0x09,ZERO			;
VECTOR 0x0a,ERROR_CODE		;invalid tss
VECTOR 0x0b,ERROR_CODE		;segment not present
VECTOR 0x0c,ZERO			;stack segment fault
VECTOR 0x0d,ERROR_CODE		;general protection
VECTOR 0x0e,ERROR_CODE		;page fault
VECTOR 0x0f,ZERO			;reserve
VECTOR 0x10,ZERO			;floating-point err
VECTOR 0x11,ERROR_CODE		;alignment check
VECTOR 0x12,ZERO			;machine check
VECTOR 0x13,ZERO			;simd floating-point exception
VECTOR 0x14,ZERO			;20~31 reserve
VECTOR 0x15,ZERO
VECTOR 0x16,ZERO
VECTOR 0x17,ZERO 
VECTOR 0x18,ERROR_CODE
VECTOR 0x19,ZERO
VECTOR 0x1a,ERROR_CODE
VECTOR 0x1b,ERROR_CODE 
VECTOR 0x1c,ZERO
VECTOR 0x1d,ERROR_CODE
VECTOR 0x1e,ERROR_CODE
VECTOR 0x1f,ZERO 
VECTOR 0x20,ZERO
;32~255 maskable interrupt

