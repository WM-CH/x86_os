#include "dir.h"
#include "stdint.h"
#include "inode.h"
#include "file.h"
#include "fs.h"
#include "stdio-kernel.h"
#include "global.h"
#include "debug.h"
#include "memory.h"
#include "string.h"
#include "interrupt.h"
#include "super_block.h"

struct dir root_dir;	// 根目录

/* 打开根目录 */
void open_root_dir(struct partition* part) {
	root_dir.inode = inode_open(part, part->sb->root_inode_no);
	root_dir.dir_pos = 0;
}

/* 在分区part上打开i结点为inode_no的目录，并返回目录指针 */
struct dir* dir_open(struct partition* part, uint32_t inode_no) {
	struct dir* pdir = (struct dir*)sys_malloc(sizeof(struct dir));	//除根目录以外的其他目录，要分配内存
	pdir->inode = inode_open(part, inode_no);
	pdir->dir_pos = 0;
	return pdir;
}

/* 在part分区内的pdir目录内，寻找名为name的目录项，
 * 找到后返回true，并将其目录项存入dir_e；否则返回false */
bool search_dir_entry(struct partition* part, struct dir* pdir, const char* name, struct dir_entry* dir_e) {
	/* 1分配 */
	uint32_t block_cnt = 140;	 // 12个直接块+128个一级间接块 = 目录的inode管理的所有块 = 140块

	/* 12个直接块大小+128个间接块,共560字节（140个块，inode需要管理块的地址，每个4字节）
	注意: all_blocks 里面并没有一级间接表的地址，只有直接块+间接块的地址！
	用all_blocks检索inode管理的所有块地址 */
	uint32_t* all_blocks = (uint32_t*)sys_malloc(48 + 512);
	if (all_blocks == NULL) {
		printk("search_dir_entry: sys_malloc for all_blocks failed");
		return false;
	}

	uint32_t block_idx = 0;
	while (block_idx < 12) {
		all_blocks[block_idx] = pdir->inode->i_sectors[block_idx];
		block_idx++;
	}
	block_idx = 0;

	if (pdir->inode->i_sectors[12] != 0) {	// i_sectors[12] != 0代表含有一级间接块表
		//从硬盘的扇区地址i_sectors[12]处获取 1 扇区数据，就是 128 个间接块的地址，将其复制到 all_blocks+12 处【复制的是整个一级间接表】
		ide_read(part->my_disk, pdir->inode->i_sectors[12], all_blocks + 12, 1);
	}
	/* 至此,all_blocks存储的是该文件或目录的所有块（扇区）地址 */


	/* 2查找 */
	/* 写目录项的时候已保证目录项不跨扇区，这样读目录项时容易处理，只申请容纳1个扇区的内存
	 * 和处理inode_table不同，在往目录中写目录项的时候(sync_dir_entry)，避免了某个目录项 跨扇区的情况 */
	uint8_t* buf = (uint8_t*)sys_malloc(SECTOR_SIZE);
	struct dir_entry* p_de = (struct dir_entry*)buf;		// p_de为指向目录项的指针,值为buf起始地址
	uint32_t dir_entry_size = part->sb->dir_entry_size;
	uint32_t dir_entry_cnt = SECTOR_SIZE / dir_entry_size;	// 1扇区内可容纳的目录项个数
	/* 在所有块中查找目录项 */
	while (block_idx < block_cnt) {
		/* 块地址为0时表示该块中无数据,继续在其它块中找 */
		if (all_blocks[block_idx] == 0) {
			block_idx++;
			continue;
		}
		ide_read(part->my_disk, all_blocks[block_idx], buf, 1);

		uint32_t dir_entry_idx = 0;
		/* 遍历扇区中所有目录项 */
		while (dir_entry_idx < dir_entry_cnt) {
			/* 若找到了,就直接复制整个目录项 */
			if (!strcmp(p_de->filename, name)) {
				memcpy(dir_e, p_de, dir_entry_size);
				sys_free(buf);
				sys_free(all_blocks);
				return true;
			}
			dir_entry_idx++;
			p_de++;
		}

		block_idx++;					// 目录inode管理的下一个数据块（扇区）【目录里面是目录项，所以buf赋给p_de】
		p_de = (struct dir_entry*)buf;	// p_de已经指向上一个扇区内，最后一个目录项，需要恢复p_de指向为buf
		memset(buf, 0, SECTOR_SIZE);	// 将buf清0,下次再用
	}
	sys_free(buf);
	sys_free(all_blocks);
	return false;
}

/* 关闭目录 */
void dir_close(struct dir* dir) {
	/*************      根目录不能关闭     ***************
	*1 根目录自打开后就不应该关闭,否则还需要再次open_root_dir();
	*2 root_dir所在的内存是低端1M之内,并非在堆中,free会出问题 */
	if (dir == &root_dir) {
		return;
	}
	inode_close(dir->inode);
	sys_free(dir);
}

/* 在内存中初始化目录项p_de */
void create_dir_entry(char* filename, uint32_t inode_no, uint8_t file_type, struct dir_entry* p_de) {
	ASSERT(strlen(filename) <=  MAX_FILE_NAME_LEN);

	/* 初始化目录项 */
	memcpy(p_de->filename, filename, strlen(filename));
	p_de->i_no = inode_no;
	p_de->f_type = file_type;
}

/* 将目录项 p_de 写入硬盘中的父目录 parent_dir 所在的inode管理的某个数据块 中【主要看ide_write的地方】
 * 此外，有时还要在硬盘中增加父目录 parent_dir 所在的inode管理的所有数据块的地址
 * 此外，还要更新内存中，父目录 parent_dir 所在的inode的相关元信息
 * io_buf是由主调函数提供的，里面是一个一个的目录项
 */
// 全局变量 cur_part 定义在fs.c中
// 临时变量 all_blocks 保存目录中 所有数据块的地址【4*140=560字节，去掉前12个直接块的地址48字节，后边全是间接块，后边正好是512字节】
// 注意: all_blocks 里面并没有一级间接表的地址，只有直接块+间接块的地址！
bool sync_dir_entry(struct dir* parent_dir, struct dir_entry* p_de, void* io_buf) {
	struct inode* dir_inode = parent_dir->inode;	//dir->inode：“已打开的 inode 队列”part->open_inodes 中的节点
	uint32_t dir_size = dir_inode->i_size;			//inode->i_size：当inode是目录时，代表 目录下所有目录项大小之和
	uint32_t dir_entry_size = cur_part->sb->dir_entry_size;

	ASSERT(dir_size % dir_entry_size == 0);	// dir_size应该是dir_entry_size的整数倍

	uint32_t dir_entrys_per_sec = (512 / dir_entry_size);	// 每扇区最多有几个目录项【保证了：写入目录项时不会跨扇区】
	int32_t block_lba = -1;


	/* 将该目录inode管理的所有块(扇区)地址(12个直接块+ 128个间接块)存入all_blocks */
	uint8_t block_idx = 0;
	// all_blocks 保存目录中 所有数据块的地址【4*140=560字节，去掉前12个直接块的地址48字节，后边全是间接块，后边正好是512字节】
	// 注意: all_blocks 里面并没有一级间接表的地址，只有直接块+间接块的地址！
	uint32_t all_blocks[140] = {0};
	while (block_idx < 12) {
		all_blocks[block_idx] = dir_inode->i_sectors[block_idx];
		block_idx++;
	}


	/* 开始遍历所有块以寻找目录项空位 */
	block_idx = 0;
	while (block_idx < 140) {	// 文件(包括目录)最多管理140个块
		/* 一、若inode管理的第block_idx块是不存在的，需要分配。
		   下边是需要分配块的三种情况 @1 @2 @3 */
		int32_t block_bitmap_idx = -1;
		if (all_blocks[block_idx] == 0) {
			block_lba = block_bitmap_alloc(cur_part);	// #1 从分区的block位图中，分配1个扇区，返回"扇区地址"
			if (block_lba == -1) {
				printk("alloc block bitmap for sync_dir_entry failed\n");
				return false;
			}
			/* 每分配一个块就同步一次block_bitmap */
			block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
			ASSERT(block_bitmap_idx != -1);	//怎么可能是-1啊。。。？
			bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);

			block_bitmap_idx = -1;
			if (block_idx < 12) {			// @1 若是直接块
				dir_inode->i_sectors[block_idx] = all_blocks[block_idx] = block_lba;
			} else if (block_idx == 12) {	// @2 若是未分配 一级间接块表(block_idx==12表示第0个间接块)
				dir_inode->i_sectors[12] = block_lba;		// 将 #1 分配的块做为"一级间接块表"地址
				
				block_lba = -1;
				block_lba = block_bitmap_alloc(cur_part);	// #2 再分配一个块做为"第0个间接块"
				if (block_lba == -1) {
					// 回滚block_bitmap
					block_bitmap_idx = dir_inode->i_sectors[12] - cur_part->sb->data_start_lba;
					bitmap_set(&cur_part->block_bitmap, block_bitmap_idx, 0);	//释放 #1
					// 取消掉这个一级间接块表
					dir_inode->i_sectors[12] = 0;
					printk("alloc block bitmap for sync_dir_entry failed\n");
					return false;
				}
				/* 每分配一个块就同步一次block_bitmap */
				block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
				ASSERT(block_bitmap_idx != -1);
				bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);

				/* 把新分配的"第0个间接块地址"写入硬盘中"一级间接块表" */
				all_blocks[12] = block_lba;
				// all_blocks 共有4*140=560字节，去掉前12个直接块的地址48字节，后边全是间接块，后边正好是512字节】
				// 注意: all_blocks 里面并没有一级间接表的地址，只有直接块+间接块的地址！
				// 我们的一级间接表大小是和普通数据块大小一样的512字节，可以容纳128个数据块的地址！
				ide_write(cur_part->my_disk, dir_inode->i_sectors[12], all_blocks + 12, 1);
			} else {						// @3 若是未分配 间接块【父目录inode管理的只有前13个数据块需要特殊处理，后边的全是间接块】
				/* 把新分配的第(block_idx-12)个间接块地址"写入硬盘中"一级间接块表 */
				all_blocks[block_idx] = block_lba;
				ide_write(cur_part->my_disk, dir_inode->i_sectors[12], all_blocks + 12, 1);
			}


			/* 这里才是重点，前边只是处理，父目录indoe管理的某个数据块为空的情况，需要新分配数据块，包括一级间接块表、间接块 */
			/* 再将硬盘中，新目录项p_de写入新分配的间接块 */
			memset(io_buf, 0, 512);
			memcpy(io_buf, p_de, dir_entry_size);
			ide_write(cur_part->my_disk, all_blocks[block_idx], io_buf, 1);
			dir_inode->i_size += dir_entry_size;	//struct inode* dir_inode = parent_dir->inode;更新的是父目录的inode结构
			return true;
		}//if (all_blocks[block_idx] == 0)


		/* 二、若inode管理的第block_idx块是存在的，不需要分配，将其读进内存，然后在该块中查找空目录项，将p_de填入 */
		ide_read(cur_part->my_disk, all_blocks[block_idx], io_buf, 1); 
		/* 在扇区内查找空目录项 */
		uint8_t dir_entry_idx = 0;
		struct dir_entry* dir_e = (struct dir_entry*)io_buf;		// dir_e用来在io_buf中遍历目录项【io_buf里面是一个一个的目录项】
		while (dir_entry_idx < dir_entrys_per_sec) {
			if ((dir_e + dir_entry_idx)->f_type == FT_UNKNOWN) {	// 无论是初始化或是删除文件，都会将f_type置为FT_UNKNOWN.
				memcpy(dir_e + dir_entry_idx, p_de, dir_entry_size);    
				ide_write(cur_part->my_disk, all_blocks[block_idx], io_buf, 1);

				dir_inode->i_size += dir_entry_size;
				return true;
			}
			dir_entry_idx++;
		}


		/* 三、若inode管理的第block_idx块是存在的，不需要分配，但是都不是空闲(FT_UNKNOWN)的 */
		block_idx++;	//下一个inode管理的block
	}//while (block_idx < 140)

	printk("directory is full!\n");
	return false;
}

