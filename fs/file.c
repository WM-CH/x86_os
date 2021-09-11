#include "file.h"
#include "fs.h"
#include "super_block.h"
#include "inode.h"
#include "stdio-kernel.h"
#include "memory.h"
#include "debug.h"
#include "interrupt.h"
#include "string.h"
#include "thread.h"
#include "global.h"

#define DEFAULT_SECS 1

/* 文件表 */
struct file file_table[MAX_FILE_OPEN];

/* 从文件表 file_table 中获取一个空闲位,成功返回下标,失败返回-1 */
int32_t get_free_slot_in_global(void) {
	uint32_t fd_idx = 3;
	while (fd_idx < MAX_FILE_OPEN) {
		if (file_table[fd_idx].fd_inode == NULL) {
			break;
		}
		fd_idx++;
	}
	if (fd_idx == MAX_FILE_OPEN) {
		printk("exceed max open files\n");
		return -1;
	}
	return fd_idx;
}

/* 将 file_table 的下标，安装到，线程PCB中文件描述符数组 fd_table
 * 成功返回 fd_table 的下标（即文件描述符）,失败返回-1 */
int32_t pcb_fd_install(int32_t globa_fd_idx) {
	struct task_struct* cur = running_thread();
	uint8_t local_fd_idx = 3;	// 跨过stdin,stdout,stderr
	while (local_fd_idx < MAX_FILES_OPEN_PER_PROC) {
		if (cur->fd_table[local_fd_idx] == -1) {	// -1表示free_slot,可用
			cur->fd_table[local_fd_idx] = globa_fd_idx;
			break;
		}
		local_fd_idx++;
	}
	if (local_fd_idx == MAX_FILES_OPEN_PER_PROC) {
		printk("exceed max open files_per_proc\n");
		return -1;
	}
	return local_fd_idx;
}

/* 从分区的inode位图中，分配一个i结点，返回"inode号" */
int32_t inode_bitmap_alloc(struct partition* part) {
	int32_t bit_idx = bitmap_scan(&part->inode_bitmap, 1);
	if (bit_idx == -1) {
		return -1;
	}
	bitmap_set(&part->inode_bitmap, bit_idx, 1);
	return bit_idx;
}
   
/* 从分区的block位图中，分配1个扇区，返回"扇区地址" */
int32_t block_bitmap_alloc(struct partition* part) {
	int32_t bit_idx = bitmap_scan(&part->block_bitmap, 1);
	if (bit_idx == -1) {
		return -1;
	}
	bitmap_set(&part->block_bitmap, bit_idx, 1);
	/* 和inode_bitmap_malloc不同,此处返回的不是位图索引,而是具体可用的扇区地址 */
	return (part->sb->data_start_lba + bit_idx);
} 

/* 将内存中bitmap第bit_idx位所在的那个512字节，同步到硬盘中bitmap第bit_idx位所在的扇区 */
// 硬盘以扇区为读写单位
void bitmap_sync(struct partition* part, uint32_t bit_idx, uint8_t btmp_type) {
	uint32_t off_sec = bit_idx / 4096;			// dst(硬盘中): bit_idx 相对于位图起始lba，在后边的第几个扇区(512*8=4096)
	uint32_t off_size = off_sec * BLOCK_SIZE;	// src(内存中): bit_idx 相对于位图数组bits，以512字节为单位，在后边的第几个单位处
												// off_size是，第 bit_idx位所在位图中以 512 字节为单位的起始地址。BLOCK_SIZE=512
	uint32_t sec_lba;
	uint8_t* bitmap_off;

	/* 需要被同步到硬盘的位图只有inode_bitmap和block_bitmap */
	switch (btmp_type) {
		case INODE_BITMAP:
		sec_lba = part->sb->inode_bitmap_lba + off_sec;
		bitmap_off = part->inode_bitmap.bits + off_size;
		break;

		case BLOCK_BITMAP: 
		sec_lba = part->sb->block_bitmap_lba + off_sec;
		bitmap_off = part->block_bitmap.bits + off_size;
		break;
	}
	ide_write(part->my_disk, sec_lba, bitmap_off, 1);	// 将bitmap_off中1个扇区的数据写入硬盘sec_lba处
}

/* 创建文件,若成功则返回文件描述符,否则返回-1 */
// 全局变量cur_part定义在fs.c中
// 全局变量文件表file_table定义在本文件
/*
1. 文件需要 inode 来描述大小、位置等属性，所以创建文件就要创建其 inode。
向 inode_bitmap 申请位图来获得 inode 号，因此 inode_bitmap 会被更新
inode_table 数组中的某项也会由新的 inode 填充。
2. inode->i_sectors 是文件具体存储的扇区地址，
需要向 block_bitmap 申请可用位来获得可用的块（1块等于1扇区）因此 block_bitmap 会被更新，
分区的数据区 data_start_lba 以后的某个扇区会被分配。
3. 新增加的文件必然存在于某个目录，所以该目录的 inode->i_size 会增加个目录项的大小。
此新增加的文件对应的目录项，需要写入该目录的 inode->i_sectors[]中的某个扇区，
原有扇区可能已满，所以有可能要申请新扇区来存储目录项。【sync_dir_entry】
4. 若其中某步操作失败，需要回滚之前已成功的操作。
5. inode_bitmap、 block_bitmap、新文件的 inode 及文件所在目录的 inode，这些位于内存中已经被改变的数据要同步到硬盘。
*/
int32_t file_create(struct dir* parent_dir, char* filename, uint8_t flag) {
	/* 后续操作的公共缓冲区 */
	void* io_buf = sys_malloc(1024);	//跨扇区的数据，会操作2个扇区
	if (io_buf == NULL) {
		printk("in file_creat: sys_malloc for io_buf failed\n");
		return -1;
	}

	uint8_t rollback_step = 0;	// 用于操作失败时回滚各资源状态

	/* 1.为新文件从分区inode位图中，分配inode号 */
	int32_t inode_no = inode_bitmap_alloc(cur_part); 
	if (inode_no == -1) {
		printk("in file_creat: allocate inode failed\n");
		return -1;
	}

	/* 2.堆中分配inode节点，不可以是栈中的局部变量！
	 * 因为 file_table 中的 fd_inode 指针要指向它. */
	struct inode* new_file_inode = (struct inode*)sys_malloc(sizeof(struct inode)); 
	if (new_file_inode == NULL) {
		printk("file_create: sys_malloc for inode failded\n");
		rollback_step = 1;
		goto rollback;
	}
	inode_init(inode_no, new_file_inode);	    // 初始化inode

	/* 3.申请一个file_table数组的下标 */
	int fd_idx = get_free_slot_in_global();
	if (fd_idx == -1) {
		printk("exceed max open files\n");
		rollback_step = 2;
		goto rollback;
	}

	// 4.填充文件表中的文件结构
	file_table[fd_idx].fd_inode = new_file_inode;
	file_table[fd_idx].fd_pos = 0;
	file_table[fd_idx].fd_flag = flag;
	file_table[fd_idx].fd_inode->write_deny = false;

	struct dir_entry new_dir_entry;
	memset(&new_dir_entry, 0, sizeof(struct dir_entry));

	// create_dir_entry在内存中初始化目录项p_de，只是内存操作不会返回失败
	create_dir_entry(filename, inode_no, FT_REGULAR, &new_dir_entry);


	/* 同步内存数据到硬盘 */
	/* a 在目录parent_dir下安装目录项new_dir_entry, 写入硬盘后返回true, 否则false【dir.c】 */
	if (!sync_dir_entry(parent_dir, &new_dir_entry, io_buf)) {
		printk("sync dir_entry to disk failed\n");
		rollback_step = 3;
		goto rollback;
	}

	/* b sync_dir_entry 会改变父目录 inode 中的信息，因此，将父目录inode的内容同步到硬盘【inode.c】 */
	memset(io_buf, 0, 1024);
	inode_sync(cur_part, parent_dir->inode, io_buf);

	/* c 将新创建文件的inode内容同步到硬盘 */
	memset(io_buf, 0, 1024);
	inode_sync(cur_part, new_file_inode, io_buf);

	/* d 将inode_bitmap位图同步到硬盘【file.c】 */
	bitmap_sync(cur_part, inode_no, INODE_BITMAP);

	/* e 将创建的文件i结点添加到open_inodes链表 */
	list_push(&cur_part->open_inodes, &new_file_inode->inode_tag);
	new_file_inode->i_open_cnts = 1;


	sys_free(io_buf);
	return pcb_fd_install(fd_idx);	// 将 file_table 的下标，安装到，线程PCB中文件描述符数组 fd_table【file.c】


rollback:
	switch (rollback_step) {
		case 3:
		/* file_table 相应位清空 */
		memset(&file_table[fd_idx], 0, sizeof(struct file)); 
		case 2:
		sys_free(new_file_inode);
		case 1:
		/* inode位图 恢复 */
		bitmap_set(&cur_part->inode_bitmap, inode_no, 0);
		break;
	}
	sys_free(io_buf);
	return -1;
}

