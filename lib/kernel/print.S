TI_GDT equ  0
RPL0  equ   0
SELECTOR_VIDEO equ (0x0003<<3) + TI_GDT + RPL0

[bits 32]
section .text
;------------------------   put_char   -----------------------------
;功能描述:把栈中的1个字符写入光标所在处
;-------------------------------------------------------------------   
global put_char		;让 put_char 对外部文件可见！
put_char:
   pushad						;备份32位寄存器（push all double）
								;入栈顺序：EAX/ECX/EDX/EBX/ESP/EBP/ESI/EDI
   
   mov ax, SELECTOR_VIDEO		;不能直接把立即数送入段寄存器
   mov gs, ax					;需要保证gs中为正确的视频段选择子

;;;;;;;;;  获取当前光标位置 ;;;;;;;;;
;in/out指令：
;in  al, 21H		从21H端口读取一字节数据到AL
;in  ax, 21H		从端口地址21H读取1字节数据到AL，从端口地址22H读取1字节到AH
;out 21H, al		将AL的值写入21H端口
;out 21H, ax		将AX的值写入端口地址21H开始的连续两个字节。（port[21H]=AL,port[22h]=AH）
   ;先获得高8位
   mov dx, 0x03d4	;索引寄存器（CRT controller：AddressRegister=0x3D4 DataRegister=0x3D5）
   mov al, 0x0e		;用于提供光标位置的高8位（往DataRegister里扔0e和0f）
   out dx, al
   mov dx, 0x03d5	;通过读写数据端口0x3d5来获得或设置光标位置 
   in  al, dx		;得到了光标位置的高8位
   mov ah, al
   ;再获取低8位
   mov dx, 0x03d4
   mov al, 0x0f
   out dx, al
   mov dx, 0x03d5 
   in al, dx

   mov bx, ax					;将光标存入bx
   ;下面这行是在栈中获取待打印的字符
   ;C函数原型是put_char(uint8 char)，所以取出栈中的数据格式是uint8，所以只使用cl而不是ecx
   mov ecx, [esp + 36]			;pushad压入4×8=32字节,加上主调函数的返回地址4字节,故esp+36字节
   cmp cl, 0xd					;CR是0x0d,LF是0x0a
   jz .is_carriage_return
   cmp cl, 0xa
   jz .is_line_feed
   cmp cl, 0x8					;BS(backspace)的ascii码是8
   jz .is_backspace
   jmp .put_other	   
;;;;;;;;;;;;;;;;;;

.is_backspace:
; 当为backspace时，光标移向前一个显存位置，添加空格或空字符0
   dec bx
   shl bx,1						;逻辑左移，相当于光标值乘以2=在显存中的位置（显存中一个字符两字节）
   mov byte [gs:bx], 0x20		;将待删除的字节补为0或空格皆可
   inc bx
   mov byte [gs:bx], 0x07		;属性
   shr bx,1						;显存位置 变回成 光标值
   jmp .set_cursor
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

.put_other:
   shl bx, 1					; 光标位置是用2字节表示,将光标值乘2,表示对应显存中的偏移字节
   mov [gs:bx], cl				; ascii字符本身
   inc bx
   mov byte [gs:bx],0x07		; 字符属性
   shr bx, 1					; 显存位置 变回成 光标值
   inc bx						; 下一个光标值
   cmp bx, 2000
   jl .set_cursor				; 若光标值小于2000=80*25,表示未写到显存的最后,则去设置新的光标值
								; 若超出屏幕字符数大小(2000)则换行处理
.is_line_feed:					; 是换行符LF(\n) newline 跑到新行行首
.is_carriage_return:			; 是回车符CR(\r) return  回到当前行首
; linux的\n就表示下一行的行首（windows需要\r\n）
; 把\n和\r都处理为linux中\n的意思，也就是下一行的行首。
; 除法：
; 如果除数为8位，被除数为16位，被除数在AX中存放
; 如果除数为16位，被除数为32位，被除数的高16位=DX，低16位=AX
; 如果除数是8位，则AL存储除法操作的商，AH存储除法操作的余数；
; 如果除数是16位，则AX存储除法操作的商，DX存储除法操作的余数。
; 下边实现 "对80求模"
   xor dx, dx			; dx是被除数的高16位,清0.
   mov ax, bx			; ax是被除数的低16位.
   mov si, 80			; 
   div si				; 商ax 余dx
   sub bx, dx			; 光标值减去"除80的余数"便是取整，获得当前行行首（完成回车CR功能）

.is_carriage_return_end:
   add bx, 80			; 下一行行首（完成换行LF功能）
   cmp bx, 2000
.is_line_feed_end:
   jl .set_cursor		; 没超过2000不需要滚屏！

;屏幕行范围是0~24,滚屏的原理是将屏幕的1~24行搬运到0~23行,再将第24行用空格填充
.roll_screen:				; 若超出屏幕大小，开始滚屏
   cld						; 设置标志寄存器中的DF=0向高地址方向递增，std设置DF=1
   mov ecx, 960				; 一共有2000-80=1920个字符要搬运,共1920*2=3840字节.一次搬4字节,共3840/4=960次 
   mov esi, 0xb80a0			; 第1行行首，源地址，虚拟地址的话，应该是 0xC00B80a0
   mov edi, 0xb8000			; 第0行行首，虚拟地址的话，应该是 0xC00B8000
   rep movsd
;将最后一行填充为空白
   mov ebx, 3840			; 最后一行首字符的第一个字节偏移= 1920 * 2
   mov ecx, 80				; 一行是80字符(160字节),每次清空1字符(2字节),一行需要移动80次
.cls:
   mov word [gs:ebx], 0x0720	;0x0720是黑底白字的空格键
   add ebx, 2
   loop .cls
   mov bx, 1920				;将光标值重置为1920,最后一行的首字符.

.set_cursor:   
;功能：将光标设为bx值
;1 先设置高8位
   mov dx, 0x03d4			;索引寄存器（CRT controller：AddressRegister=0x3D4 DataRegister=0x3D5）
   mov al, 0x0e				;用于提供光标位置的高8位
   out dx, al
   mov dx, 0x03d5			;通过读写数据端口0x3d5来获得或设置光标位置 
   mov al, bh
   out dx, al
;2 再设置低8位
   mov dx, 0x03d4
   mov al, 0x0f
   out dx, al
   mov dx, 0x03d5 
   mov al, bl
   out dx, al
.put_char_done: 
   popad
   ret
