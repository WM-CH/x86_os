%include "boot.inc"
section loader vstart=LOADER_BASE_ADDR
;----------------------------------------------------------------
;构建gdt及其内部的描述符
GDT_BASE:   dd	0x00000000 
			dd	0x00000000
CODE_DESC:  dd	0x0000FFFF			;CODE/DATA/STACK段界限是 FFFFF。实际段界限=(描述符中段界限+1)*4k-1。得到实际段界限=0xFFFF_FFFF(4G)
			dd	DESC_CODE_HIGH4		;段基址是 0
DATA_STACK_DESC:
			dd	0x0000FFFF
			dd	DESC_DATA_HIGH4
VIDEO_DESC: dd	0x80000007			;video段界限是0x7，代入公式，得到实际段界限=32k，文本模式内存地址0xb8000~0xbffff=(0x7FFF)=32k
			dd	DESC_VIDEO_HIGH4	;段基址是0xb8000。注意，此时dpl=0
GDT_SIZE	equ	$ - GDT_BASE
GDT_LIMIT   equ	GDT_SIZE - 1
times 60 	dq	0					 ; 此处预留60个描述符的slot（8*4+8*60=512Byte(0x200)）
SELECTOR_CODE	equ	(0x0001<<3) + TI_GDT + RPL0	;(CODE_DESC - GDT_BASE)*8 + TI_GDT + RPL0
SELECTOR_DATA	equ (0x0002<<3) + TI_GDT + RPL0
SELECTOR_VIDEO	equ (0x0003<<3) + TI_GDT + RPL0


; total_mem_bytes用于保存内存容量,以字节为单位
; 偏移loader.bin文件头0x200字节
total_mem_bytes dd 0
;----------------------------------------------

; gdt的指针，前2字节是gdt界限，后4字节是gdt起始地址
gdt_ptr		dw  GDT_LIMIT 
			dd  GDT_BASE

; total_mem_bytes4字节+gdt_ptr6字节+ards_buf244字节+ards_nr2 = 256(0x100)字节
ards_buf	times 244 db 0
ards_nr		dw 0		      ;用于记录ards结构体数量




;----------------------------------------------------------------
loader_start:
;-------  int 15h eax = 0000E820h ,edx = 534D4150h ('SMAP') 获取内存布局  -------
	xor ebx, ebx		      ;第一次调用时，ebx值要为0
	mov edx, 0x534d4150	      ;edx只赋值一次，循环体中不会改变
	mov di, ards_buf	      ;ards结构缓冲区
.e820_mem_get_loop:	      ;循环获取每个ARDS内存范围描述结构
	mov eax, 0x0000e820	      ;执行int 0x15后,eax值变为0x534d4150,所以每次执行int前都要更新为子功能号。
	mov ecx, 20		      ;ARDS地址范围描述符结构大小是20字节
	int 0x15
	jc .e820_failed_so_try_e801   ;若cf位为1则有错误发生，尝试0xe801子功能
	add di, cx		      ;使di增加20字节指向缓冲区中新的ARDS结构位置
	inc word [ards_nr]	      ;记录ARDS数量
	cmp ebx, 0		      ;若ebx为0且cf不为1,这说明ards全部返回，当前已是最后一个
	jnz .e820_mem_get_loop

	;在所有ards结构中，找出(base_add_low + length_low)的最大值，即内存的容量。
	mov cx, [ards_nr]	      ;遍历每一个ARDS结构体,循环次数是ARDS的数量
	mov ebx, ards_buf 
	xor edx, edx		      ;edx为最大的内存容量,在此先清0
.find_max_mem_area:	      ;无须判断type是否为1,最大的内存块一定是可被使用
	mov eax, [ebx]	      ;base_add_low
	add eax, [ebx+8]	      ;length_low
	add ebx, 20		      ;指向缓冲区中下一个ARDS结构
	cmp edx, eax		      ;冒泡排序，找出最大,edx寄存器始终是最大的内存容量
	jge .next_ards
	mov edx, eax		      ;edx为总内存大小
.next_ards:
	loop .find_max_mem_area
	jmp .mem_get_ok

	;------  int 15h ax = E801h 获取内存大小,最大支持4G  ------
	; 返回后, ax cx 值一样,以KB为单位,bx dx值一样,以64KB为单位
	; 在ax和cx寄存器中为低16M,在bx和dx寄存器中为16MB到4G。
.e820_failed_so_try_e801:
	mov ax,0xe801
	int 0x15
	jc .e801_failed_so_try88   ;若当前e801方法失败,就尝试0x88方法

	;1 先算出低15M的内存,ax和cx中是以KB为单位的内存数量,将其转换为以byte为单位
	mov cx,0x400	     ;cx和ax值一样,cx用做乘数
	mul cx 
	shl edx,16
	and eax,0x0000FFFF
	or edx,eax
	add edx, 0x100000 ;ax只是15MB,故要加1MB
	mov esi,edx	     ;先把低15MB的内存容量存入esi寄存器备份

	;2 再将16MB以上的内存转换为byte为单位,寄存器bx和dx中是以64KB为单位的内存数量
	xor eax,eax
	mov ax,bx		
	mov ecx, 0x10000	;0x10000十进制为64KB
	mul ecx		;32位乘法,默认的被乘数是eax,积为64位,高32位存入edx,低32位存入eax.
	add esi,eax		;由于此方法只能测出4G以内的内存,故32位eax足够了,edx肯定为0,只加eax便可
	mov edx,esi		;edx为总内存大小
	jmp .mem_get_ok

	;-----------------  int 15h ah = 0x88 获取内存大小,只能获取64M之内  ----------
.e801_failed_so_try88: 
	;int 15后，ax存入的是以kb为单位的内存容量
	mov  ah, 0x88
	int  0x15
	jc .error_hlt
	and eax,0x0000FFFF
	  
	;16位乘法，被乘数是ax,积为32位.积的高16位在dx中，积的低16位在ax中
	mov cx, 0x400     ;0x400等于1024,将ax中的内存容量换为以byte为单位
	mul cx
	shl edx, 16	     ;把dx移到高16位
	or edx, eax	     ;把积的低16位组合到edx,为32位的积
	add edx,0x100000  ;0x88子功能只会返回1MB以上的内存,故实际内存大小要加上1MB

.mem_get_ok:
	mov [total_mem_bytes], edx	 ;将内存换为byte单位后存入total_mem_bytes处。

	;使用mbr中设置的段寄存器
	mov byte [gs:80*2+0x00], 'L'
	mov byte [gs:80*2+0x01], 0x07
	mov byte [gs:80*2+0x02], 'o'
	mov byte [gs:80*2+0x03], 0x07
	mov byte [gs:80*2+0x04], 'a'
	mov byte [gs:80*2+0x05], 0x07
	mov byte [gs:80*2+0x06], 'd'
	mov byte [gs:80*2+0x07], 0x07
	mov byte [gs:80*2+0x08], 'e'
	mov byte [gs:80*2+0x09], 0x07
	mov byte [gs:80*2+0x0a], 'r'
	mov byte [gs:80*2+0x0b], 0x07
	
	;进入保护模式三步：
	;-----------------  打开A20  ----------------
	in al,0x92
	or al,0000_0010B
	out 0x92,al
	;-----------------  加载GDT  ----------------
	lgdt [cs:gdt_ptr]
	;-----------------  cr0第0位置1  ----------------
	mov eax, cr0
	or eax, 0x00000001
	mov cr0, eax

	;刷新流水线，避免分支预测的影响,这种cpu优化策略，最怕jmp跳转，
	jmp  SELECTOR_CODE:p_mode_start

.error_hlt:		      ;出错则挂起
	hlt

[bits 32]
p_mode_start:
	;进入保护模式后，段寄存器必须初始化，之前的值是实模式的！
	mov ax, SELECTOR_DATA
	mov ds, ax
	mov es, ax
	mov ss, ax
	mov esp,LOADER_STACK_TOP
	mov ax, SELECTOR_VIDEO
	mov gs, ax

	mov byte [gs:80*2+0x0e], 'P'
	mov byte [gs:80*2+0x10], 'M'
	mov byte [gs:80*2+0x12], 'o'
	mov byte [gs:80*2+0x14], 'd'
	mov byte [gs:80*2+0x16], 'e'
	
; -------------------------   加载kernel  ----------------------
	mov eax, KERNEL_START_SECTOR        ; kernel.bin所在的扇区号
	mov ebx, KERNEL_BIN_BASE_ADDR       ; 从磁盘读出后，写入到ebx指定的地址
	mov ecx, 200			       ; 读入的扇区数
	call rd_disk_m_32
	
	; 创建页目录及页表并初始化页内存位图
	call setup_page

	
	sgdt [gdt_ptr]
	;将gdt描述符中，视频段描述符，其段基址+0xC0000000
	mov ebx, [gdt_ptr + 2]					;取得GDT_BASE
	or dword [ebx + 0x18 + 4], 0xC0000000	;8*3=24=0x18
	
	;将gdt的基址加上0xc0000000使其成为内核所在的高地址
	add dword [gdt_ptr + 2], 0xC0000000

	add esp, 0xC0000000        ; 将栈指针同样映射到内核地址

	
	; 把页目录地址赋给cr3
	mov eax, PAGE_DIR_TABLE_POS
	mov cr3, eax
	; 打开cr0的pg位(第31位)，开启分页
	mov eax, cr0
	or eax, 0x80000000
	mov cr0, eax


	;在开启分页后,用gdt新的地址重新加载
	lgdt [gdt_ptr]

	jmp SELECTOR_CODE:enter_kernel	  ;强制刷新流水线,更新gdt（是刷新TLB吧！）
;=============================================================================
enter_kernel:
	call kernel_init
	mov esp, 0xc009f000
	jmp KERNEL_ENTRY_POINT                 ; 用地址0x1500访问测试，结果ok


;-----------------   将kernel.bin中的segment拷贝到编译的地址   -----------
;解析elf文件
kernel_init:
	xor eax, eax
	xor ebx, ebx		;ebx记录程序头表地址
	xor ecx, ecx		;cx记录程序头表中的program header数量
	xor edx, edx		;dx 记录program header尺寸,即e_phentsize

	mov dx,		[KERNEL_BIN_BASE_ADDR + 42]	  	; 偏移文件42字节处的属性是e_phentsize：		program header大小
	mov ebx,	[KERNEL_BIN_BASE_ADDR + 28]   	; 偏移文件开始部分28字节的地方是e_phoff：	第1个program header在文件中的偏移量
												; 其实该值是0x34,不过还是谨慎一点，这里来读取实际值
	add ebx,	KERNEL_BIN_BASE_ADDR
	mov cx,		[KERNEL_BIN_BASE_ADDR + 44]		; 偏移文件开始部分44字节的地方是e_phnum:	有几个program header
.each_segment:
	cmp byte	[ebx + 0], PT_NULL		; 若p_type等于 PT_NULL，说明此program header未使用。
	je .PTNULL

	;为函数memcpy压入参数,参数是从右往左依次压入。memcpy(dst,src,size)
	push dword	[ebx + 16]				; program header中偏移16字节的地方是p_filesz。压入函数memcpy的第三个参数:size
	mov eax,	[ebx + 4]				; 距程序头偏移量为4字节的位置是p_offset
	add eax,	KERNEL_BIN_BASE_ADDR	; 加上kernel.bin被加载到的物理地址，eax为该段的物理地址
	push eax							; 压入函数memcpy的第二个参数:源地址
	push dword	[ebx + 8]				; 压入函数memcpy的第一个参数:目的地址，程序头8字节的位置是p_vaddr = 目的地址
	call 		mem_cpy					; 调用mem_cpy完成段复制
	add esp,	12						; 清理栈中压入的三个参数
.PTNULL:
	add ebx, edx						; edx为program header大小，即e_phentsize，在此ebx指向下一个program header 
	loop .each_segment
	ret

;----------  逐字节拷贝 mem_cpy(dst,src,size) ------------
;输入:栈中三个参数(dst,src,size)
;输出:无
;---------------------------------------------------------
mem_cpy:		      
	cld
	push ebp
	mov ebp, esp
	push ecx				; rep指令用到了ecx
	mov edi, [ebp + 8]		; dst
	mov esi, [ebp + 12]		; src
	mov ecx, [ebp + 16]		; size
	rep movsb				; 逐字节拷贝
	;恢复环境
	pop ecx
	pop ebp
	ret


;-------------   创建页目录及页表   ---------------
;PageDirectoryEntry PDE 
;PageTableEntry		PTE
setup_page:
	;页目录清0
	mov ecx, 4096
	mov esi, 0
.clear_page_dir:
	mov byte [PAGE_DIR_TABLE_POS + esi], 0
	inc esi
	loop .clear_page_dir

	;创建页目录项(PDE)
.create_pde:
	mov eax, PAGE_DIR_TABLE_POS
	add eax, 0x1000 			     ; 第一个页表的位置及属性
	mov ebx, eax				     ; .create_pte中，ebx为基址

; 0xC00以上的目录项用于内核空间, 也就是页表的0xc0000000~0xffffffff共计1G属于内核,0x0~0xbfffffff共计3G属于用户进程
; 页目录项0和0xC00(=768*4)，指向第一个页表的地址，
; 一个页表指示4MB内存，0x003Fffff和0xC03Fffff虚拟内存区域，使用相同的页表，
; 为内核地址做准备
	or eax, PG_US_U | PG_RW_W | PG_P			; 页目录项的属性RW和P位为1、US为1，表示用户属性，所有特权级别都可以访问
	mov [PAGE_DIR_TABLE_POS + 0x0], eax			; 第1个目录项，在页目录表中的第1个目录项，写入第一个页表的位置(0x101000)及属性(3)
	mov [PAGE_DIR_TABLE_POS + 0xc00], eax		; 第768个PDE，也写入第一个页表的位置(0x101000)及属性(3)
	sub eax, 0x1000
	mov [PAGE_DIR_TABLE_POS + 4092], eax		; 使最后一个目录项指向页目录表自己

;下面创建页表项(PTE)
	mov ecx, 256				     	; 1M低端内存 / 每页大小4k = 256个PTE
	mov esi, 0
	mov edx, PG_US_U | PG_RW_W | PG_P	; 属性为7：US=1/RW=1/P=1
.create_pte:
	mov [ebx+esi*4],edx					; ebx=0x101000=第一个页表的地址
	add edx,4096
	inc esi
	loop .create_pte

	;创建内核其它页表的PDE
	mov eax, PAGE_DIR_TABLE_POS
	add eax, 0x2000						; eax=第二个页表的位置(0x102000)
	or eax, PG_US_U | PG_RW_W | PG_P	; 页目录项的属性RW和P位为1、US为0
	mov ebx, PAGE_DIR_TABLE_POS
	mov ecx, 254						; 范围为第769~1022的所有目录项，ecx=数量
	mov esi, 769
.create_kernel_pde:
	mov [ebx+esi*4], eax				;ebx索引目录项，eax索引第二个页表之后的页表
	inc esi								;下一个目录项
	add eax, 0x1000						;下一个页表(0x1000=4k)
	loop .create_kernel_pde
	ret


;-------------------------------------------------------------------------------
;功能:读取硬盘n个扇区
; eax=LBA扇区号
; ebx=将数据写入的内存地址
; ecx=读入的扇区数
rd_disk_m_32:
      mov esi,eax
      mov di,cx
;读写硬盘:
;第1步：设置要读取的扇区数
      mov dx,0x1f2
      mov al,cl
      out dx,al

      mov eax,esi

;第2步：将LBA地址存入0x1f3 ~ 0x1f6

      mov dx,0x1f3                       
      out dx,al                          

      mov cl,8
      shr eax,cl
      mov dx,0x1f4
      out dx,al

      shr eax,cl
      mov dx,0x1f5
      out dx,al

      shr eax,cl
      and al,0x0f
      or al,0xe0
      mov dx,0x1f6
      out dx,al

;第3步：向0x1f7端口写入读命令，0x20 
      mov dx,0x1f7
      mov al,0x20                        
      out dx,al

;第4步：检测硬盘状态
  .not_ready:
      nop
      in al,dx
      and al,0x88
      cmp al,0x08
      jnz .not_ready

;第5步：从0x1f0端口读数据
      mov ax, di
      mov dx, 256
      mul dx
      mov cx, ax	   
      mov dx, 0x1f0
  .go_on_read:
      in ax,dx		
      mov [ebx], ax
      add ebx, 2
			  ; 由于在实模式下偏移地址为16位,所以用bx只会访问到0~FFFFh的偏移。
			  ; loader的栈指针为0x900,bx为指向的数据输出缓冲区,且为16位，
			  ; 超过0xffff后,bx部分会从0开始,所以当要读取的扇区数过大,待写入的地址超过bx的范围时，
			  ; 从硬盘上读出的数据会把0x0000~0xffff的覆盖，
			  ; 造成栈被破坏,所以ret返回时,返回地址被破坏了,已经不是之前正确的地址,
			  ; 故程序出会错,不知道会跑到哪里去。
			  ; 所以改为ebx代替bx指向缓冲区,这样生成的机器码前面会有0x66和0x67来反转。
			  ; 0X66用于反转默认的操作数大小! 0X67用于反转默认的寻址方式.
			  ; cpu处于16位模式时,会理所当然的认为操作数和寻址都是16位,处于32位模式时,
			  ; 也会认为要执行的指令是32位.
			  ; 当我们在其中任意模式下用了另外模式的寻址方式或操作数大小(姑且认为16位模式用16位字节操作数，
			  ; 32位模式下用32字节的操作数)时,编译器会在指令前帮我们加上0x66或0x67，
			  ; 临时改变当前cpu模式到另外的模式下.
			  ; 假设当前运行在16位模式,遇到0X66时,操作数大小变为32位.
			  ; 假设当前运行在32位模式,遇到0X66时,操作数大小变为16位.
			  ; 假设当前运行在16位模式,遇到0X67时,寻址方式变为32位寻址
			  ; 假设当前运行在32位模式,遇到0X67时,寻址方式变为16位寻址.

      loop .go_on_read
      ret

	
	
	
	
	
	
	
	
	jmp $

