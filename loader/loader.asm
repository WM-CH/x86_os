%include "boot.inc"
LOADER_STACK_TOP equ LOADER_BASE_ADDR
section loader vstart=LOADER_BASE_ADDR
	jmp loader_start
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
times 60 	dq	0					 ; 此处预留60个描述符的slot
SELECTOR_CODE	equ	(0x0001<<3) + TI_GDT + RPL0	;(CODE_DESC - GDT_BASE)*8 + TI_GDT + RPL0
SELECTOR_DATA	equ (0x0002<<3) + TI_GDT + RPL0
SELECTOR_VIDEO	equ (0x0003<<3) + TI_GDT + RPL0

;gdt的指针，前2字节是gdt界限，后4字节是gdt起始地址
gdt_ptr		dw  GDT_LIMIT 
			dd  GDT_BASE
;----------------------------------------------------------------
loader_start:
	mov ax, 0xb800
	mov ds, ax
	
	mov byte [80*2+0x00], 'L'
	mov byte [80*2+0x01], 0x07
	mov byte [80*2+0x02], 'o'
	mov byte [80*2+0x03], 0x07
	mov byte [80*2+0x04], 'a'
	mov byte [80*2+0x05], 0x07
	mov byte [80*2+0x06], 'd'
	mov byte [80*2+0x07], 0x07
	mov byte [80*2+0x08], 'e'
	mov byte [80*2+0x09], 0x07
	mov byte [80*2+0x0a], 'r'
	mov byte [80*2+0x0b], 0x07
	
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

[bits 32]
p_mode_start:
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
	jmp $

