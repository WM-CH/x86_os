	mov ax, 0xb800
	mov ds, ax
	
	mov byte [80*2+0x00], 'L'
	mov byte [80*2+0x01], 0xA4
	mov byte [80*2+0x02], 'o'
	mov byte [80*2+0x03], 0xA4
	mov byte [80*2+0x04], 'd'
	mov byte [80*2+0x05], 0xA4
	mov byte [80*2+0x06], 'd'
	mov byte [80*2+0x07], 0xA4
	mov byte [80*2+0x08], 'e'
	mov byte [80*2+0x09], 0xA4
	mov byte [80*2+0x0a], 'r'
	mov byte [80*2+0x0b], 0xA4
	
	jmp $