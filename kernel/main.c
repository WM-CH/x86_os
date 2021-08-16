#include "print.h"
#include "init.h"
#include "memory.h"
int main(void) {
   put_str("I am kernel\n");
   init_all();
   
   void* addr = get_kernel_pages(3);
   put_str("\n get_kernel_page start vaddr is ");
   put_int((uint32_t)addr);
   put_str("\n");

   while(1);
   return 0;
}

/*
I am kernel
init_all
idt_init start
	idt_desc_init done
	pic_init done
idt_init done
timer_init start
timer_init done
mem_init start
	mem_pool_init start
		kernel_pool_bitmap_start:C009A000 kernel_pool_phy_addr_start:200000
		user_pool_bitmap_start:C009A1E0 user_pool_phy_addr_start :1100000
	mem_pool_init done
mem_init done
get_kernel_page start vaddris C0100000
*/







