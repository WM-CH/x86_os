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
	
	mov ah, 3		;获取光标位置
	mov bh, 0
	int 0x10
	
	mov ax, msg
	mov bp, ax		; es:bp是字符串首地址【es=0x7c0而不是0x7c00！】
	mov cx, 5		; 字符串长度
	mov bx, 0x2		; bh 存储要显示的页号,此处是第 0 页
					; bl 中是字符属性,属性黑底绿字(bl = 02h)
	
	mov ax, 0x1301
	int 0x10

	jmp $

	msg db "MBR01"
	
current:
	times 510 - (current-start) db 0

db 0x55
db 0xaa