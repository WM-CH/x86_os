LOADER_BASE_ADDR equ 0x900 
LOADER_START_SECTOR equ 0x2

start:
	mov ax, 0xb800
	mov ds, ax
	mov ax, 0x7c0
	mov es, ax

	
	mov ax, 0x600	;清屏
	mov bx, 0x700
	mov cx, 0
	mov dx, 0x184f
	int 0x10
	
	mov byte [0x00], 'M'
	mov byte [0x01], 0xA4
	mov byte [0x02], 'B'
	mov byte [0x03], 0xA4
	mov byte [0x04], 'R'
	mov byte [0x05], 0xA4

	
	mov eax, LOADER_START_SECTOR
	mov bx , LOADER_BASE_ADDR
	mov cx , 1
	call rd_disk_m_16
	
	jmp LOADER_BASE_ADDR

;--------------------------------------
;eax = LBA
;bx  = 目的地址
;cx  = 要读入的扇区数
rd_disk_m_16:
	;备份
	mov esi, eax
	mov di,  cx
	
	;1.设置扇区数
	mov dx,  0x1f2
	mov al,  cl
	out dx,  al
	mov eax, esi	;恢复
	
	;2.设置LBA，每次8位
	mov dx,  0x1f3
	out dx,  al
	
	mov cl,  8
	shr eax, cl
	mov dx,  0x1f4
	out dx,  al
	
	shr eax, cl
	mov dx,  0x1f5
	out dx,  al
	
	shr eax, cl
	and al,  0x0f
	or  al,  0xe0
	mov dx,  0x1f6
	out dx,  al
	
	;3.读命令0x20
	mov dx,  0x1f7
	mov al,  0x20
	out dx,  al
	
	;4.检测硬盘状态
not_ready:
	nop
	in  al,  dx
	and al,  0x88
	cmp al,  0x08	; bit7=1:busy
	jnz not_ready
	
	;5.读数据
	mov ax,  di		;扇区数
	mov dx,  256
	mul dx
	mov cx,  ax		;每次读一个字，次数=扇区数*512/2
	mov dx,  0x1f0
go_on_read:
	in  ax,   dx
	mov [bx], ax
	add bx,   2
	loop go_on_read
	ret
	
current:
	times 510 - (current-start) db 0

dw 0xaa55