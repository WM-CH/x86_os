LOADER_BASE_ADDR equ 0x900 
section loader vstart=0x900
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
	
	jmp $