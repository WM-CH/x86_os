#include "fs.h"
#include "super_block.h"
#include "inode.h"
#include "dir.h"
#include "stdint.h"
#include "stdio-kernel.h"
#include "list.h"
#include "string.h"
#include "ide.h"
#include "global.h"
#include "debug.h"
#include "memory.h"


struct partition* cur_part;	 // 默认操作的分区

/*
挂载分区：
把该分区文件系统的元信息从硬盘上读出来加载到内存中，
这样硬盘资源的变化都用内存中元信息来跟踪，
如果有写操作，及时将内存中的元信息同步写入到硬盘以持久化。
*/
static bool mount_partition(struct list_elem* pelem, int arg) {
	char* part_name = (char*)arg;	// sdb1
	struct partition* part = elem2entry(struct partition, part_tag, pelem);	// 根据pelem找到partition
	if (!strcmp(part->name, part_name)) {	//名字一致
		cur_part = part;
		struct disk* hd = cur_part->my_disk;
		struct super_block* sb_buf = (struct super_block*)sys_malloc(SECTOR_SIZE);

		/* 在内存中创建分区cur_part的超级块 */
		// super_block 占用了1个扇区，但是它后边有很多数据是pad只做填充用
		cur_part->sb = (struct super_block*)sys_malloc(sizeof(struct super_block));
		if (cur_part->sb == NULL) {
			PANIC("alloc memory failed!");
		}

		/* 读入超级块 */
		memset(sb_buf, 0, SECTOR_SIZE);
		ide_read(hd, cur_part->start_lba + 1, sb_buf, 1);
		// 拷贝给 cur_part->sb
		memcpy(cur_part->sb, sb_buf, sizeof(struct super_block));

		/* 读入块位图 */
		cur_part->block_bitmap.bits = (uint8_t*)sys_malloc(sb_buf->block_bitmap_sects * SECTOR_SIZE);
		if (cur_part->block_bitmap.bits == NULL) {
			PANIC("alloc memory failed!");
		}
		cur_part->block_bitmap.btmp_bytes_len = sb_buf->block_bitmap_sects * SECTOR_SIZE;
		ide_read(hd, sb_buf->block_bitmap_lba, cur_part->block_bitmap.bits, sb_buf->block_bitmap_sects);

		/* 读入inode位图 */
		cur_part->inode_bitmap.bits = (uint8_t*)sys_malloc(sb_buf->inode_bitmap_sects * SECTOR_SIZE);
		if (cur_part->inode_bitmap.bits == NULL) {
			PANIC("alloc memory failed!");
		}
		cur_part->inode_bitmap.btmp_bytes_len = sb_buf->inode_bitmap_sects * SECTOR_SIZE;
		ide_read(hd, sb_buf->inode_bitmap_lba, cur_part->inode_bitmap.bits, sb_buf->inode_bitmap_sects);


		list_init(&cur_part->open_inodes);	// 本分区打开的inode队列！


		printk("mount %s done!\n", part->name);
		/* 返回true时list_traversal才会停止遍历,减少了后面元素无意义的遍历. */
		return true;
	}//if (!strcmp(part->name, part_name))
	return false;	// list_traversal继续遍历
}


/****************************************************************************************
	格式化分区, 创建文件系统
	就是把文件系统元信息，写到硬盘上！
	+------------------------------------------------------------------------------+
	| 整个硬盘：                                                                   |
	| MBR引导扇区，分区1，分区2 ... 分区N                                          |
	|               ↑                                                              |
	| 某个分区内的结构：                                                           |
	| 操作系统引导块，超级块，空闲块位图，inode位图，inode数组，根目录，空闲块区域 |
	+------------------------------------------------------------------------------+
	partition结构见ide.h
	MAX_FILES_PER_PART == 4096
****************************************************************************************/
static void partition_format(struct partition* part) {
	/* 为方便实现，inode管理的数据块，一个块大小是一扇区 */
	uint32_t boot_sector_sects = 1;		//引导块
	uint32_t super_block_sects = 1;		//超级块
	uint32_t inode_bitmap_sects = DIV_ROUND_UP(MAX_FILES_PER_PART, BITS_PER_SECTOR);	// inode位图占几个扇区.最多支持4096个文件
	uint32_t inode_table_sects = DIV_ROUND_UP((sizeof(struct inode) * MAX_FILES_PER_PART), SECTOR_SIZE);	//inode数组占几个扇区
	uint32_t used_sects = boot_sector_sects + super_block_sects + inode_bitmap_sects + inode_table_sects;
	uint32_t free_sects = part->sec_cnt - used_sects;  

	/************** 简单处理块位图占据的扇区数 ***************/
	uint32_t block_bitmap_sects;	//空闲块位图占几个扇区
	block_bitmap_sects = DIV_ROUND_UP(free_sects, BITS_PER_SECTOR);		//free_sects空闲块位图要占几个扇区
	/* 再算一次空闲块数量 */
	uint32_t block_bitmap_bit_len = free_sects - block_bitmap_sects;	//block_bitmap_bit_len 是位图长度，也是空闲块数量
	block_bitmap_sects = DIV_ROUND_UP(block_bitmap_bit_len, BITS_PER_SECTOR); 
	/*********************************************************/

	/* 超级块初始化 */
	struct super_block sb;		//局部变量在栈中512字节
	sb.magic = 0x19590318;
	sb.sec_cnt = part->sec_cnt;
	sb.inode_cnt = MAX_FILES_PER_PART;
	sb.part_lba_base = part->start_lba;

	sb.block_bitmap_lba = sb.part_lba_base + 2;	// 第0块是引导块,第1块是超级块
	sb.block_bitmap_sects = block_bitmap_sects;	// 空闲块位图占几个扇区

	sb.inode_bitmap_lba = sb.block_bitmap_lba + sb.block_bitmap_sects;
	sb.inode_bitmap_sects = inode_bitmap_sects;	// inode位图占几个扇区

	sb.inode_table_lba = sb.inode_bitmap_lba + sb.inode_bitmap_sects;
	sb.inode_table_sects = inode_table_sects;	// inode数组占几个扇区

	sb.data_start_lba = sb.inode_table_lba + sb.inode_table_sects;	// 数据区开始的第一个扇区号

	sb.root_inode_no = 0;	//根目录的inode号是0
	sb.dir_entry_size = sizeof(struct dir_entry);

	printk("%s info:\n", part->name);
	printk("   magic:0x%x\n   part_lba_base:0x%x\n   all_sectors:0x%x\n   inode_cnt:0x%x\n   block_bitmap_lba:0x%x\n   block_bitmap_sectors:0x%x\n   inode_bitmap_lba:0x%x\n   inode_bitmap_sectors:0x%x\n   inode_table_lba:0x%x\n   inode_table_sectors:0x%x\n   data_start_lba:0x%x\n", sb.magic, sb.part_lba_base, sb.sec_cnt, sb.inode_cnt, sb.block_bitmap_lba, sb.block_bitmap_sects, sb.inode_bitmap_lba, sb.inode_bitmap_sects, sb.inode_table_lba, sb.inode_table_sects, sb.data_start_lba);


	/***** 1 将超级块写入本分区的1号扇区 *****/
	struct disk* hd = part->my_disk;	// 分区所属的硬盘
	// void ide_write(struct disk* hd, uint32_t lba, void* buf, uint32_t sec_cnt);
	ide_write(hd, part->start_lba + 1, &sb, 1);
	printk("   super_block_lba:0x%x\n", part->start_lba + 1);


	/* 他们三个写到硬盘上：block_bitmap_sects/inode_bitmap_sects/inode_table_sects 缓冲区选三者最大的 */
	uint32_t buf_size = (sb.block_bitmap_sects >= sb.inode_bitmap_sects ? sb.block_bitmap_sects : sb.inode_bitmap_sects);
	buf_size = (buf_size >= sb.inode_table_sects ? buf_size : sb.inode_table_sects) * SECTOR_SIZE;
	uint8_t* buf = (uint8_t*)sys_malloc(buf_size);	// 申请的内存由内存管理系统清0后返回


	/***** 2 将块位图初始化并写入sb.block_bitmap_lba *****/
	/* 初始化块位图 block_bitmap */
	buf[0] |= 0x01;	// 第0个块预留给根目录,位图中先占位
	uint32_t block_bitmap_last_byte = block_bitmap_bit_len / 8;		//block_bitmap_bit_len 是位图长度，也是空闲块数量
	uint8_t  block_bitmap_last_bit  = block_bitmap_bit_len % 8;
	uint32_t last_size = SECTOR_SIZE - (block_bitmap_last_byte % SECTOR_SIZE);	// last_size是位图占用的最后一个扇区中，不足一扇区的空闲部分

	// 1 空闲块位图最后一字节，到最后一字节所在扇区的末尾，全置为占用（空闲块位图所在扇区的空闲部分）
	memset(&buf[block_bitmap_last_byte], 0xff, last_size);

	// 2 上一步中最后一字节内，有效的位重新置0
	uint8_t bit_idx = 0;
	while (bit_idx <= block_bitmap_last_bit) {
		buf[block_bitmap_last_byte] &= ~(1 << bit_idx++);
	}
	// 3 写回到硬盘 空闲块位图 区域
	ide_write(hd, sb.block_bitmap_lba, buf, sb.block_bitmap_sects);


	/***** 3 将inode位图初始化并写入sb.inode_bitmap_lba *****/
	memset(buf, 0, buf_size);
	buf[0] |= 0x1;	// 第0个inode分给了根目录
	/* 最多4096个文件，inode位图4096个bit，正好占用1扇区，inode_bitmap_sects等于1
	 * 不像block_bitmap，在inode_bitmap的扇区中，没有多余的空闲无效位 */
	// 写回到硬盘 inode位图 区域
	ide_write(hd, sb.inode_bitmap_lba, buf, sb.inode_bitmap_sects);


	/***** 4 将inode数组初始化并写入sb.inode_table_lba *****/
	/* 处理inode_table中的第0项，即根目录所在的inode */
	memset(buf, 0, buf_size);
	struct inode* i = (struct inode*)buf;
	i->i_size = sb.dir_entry_size * 2;		// .和..	// 所有目录项大小之和
	i->i_no = 0;   // 根目录占inode数组中第0个inode
	// inode管理的是数据块（对于我们来说就是扇区）
	// 此处是指定根目录这个inode管理的数据块的位置
	// i_sectors数组后边的元素都是0 没用到
	i->i_sectors[0] = sb.data_start_lba;
	// 写到硬盘 inode数组
	// 其实我们只处理了第0项
	ide_write(hd, sb.inode_table_lba, buf, sb.inode_table_sects);


	/***** 5 将根目录初始化并写入sb.data_start_lba *****/
	/* 写入根目录的两个目录项.和.. */
	// inode不知道数据块中是普通文件，还是目录。但是目录项知道。
	memset(buf, 0, buf_size);
	struct dir_entry* p_de = (struct dir_entry*)buf;

	/* 初始化当前目录"." */
	memcpy(p_de->filename, ".", 1);
	p_de->i_no = 0;	// 目录或文件对应的inode编号
	p_de->f_type = FT_DIRECTORY;

	p_de++;

	/* 初始化当前目录父目录".." */
	memcpy(p_de->filename, "..", 2);
	p_de->i_no = 0;	// 根目录的父目录依然是根目录自己
	p_de->f_type = FT_DIRECTORY;

	/* sb.data_start_lba已经分配给了根目录，我们把根目录的目录项放进去 */
	ide_write(hd, sb.data_start_lba, buf, 1);


	printk("   root_dir_lba:0x%x\n", sb.data_start_lba);
	printk("%s format done\n", part->name);
	sys_free(buf);
}

/* 功能：
 * 1.在磁盘上搜索文件系统，若没有则格式化分区，创建文件系统。
 * 2.挂载分区
 * 
 * 只支持 partition_format 函数创建的文件系统，其魔数等于 0x19590318
 * 三层循环：遍历通道，遍历通道中的硬盘，遍历硬盘上的分区
 * 全局变量 channel_cnt 通道数，定义在 ide.c
 * 全局变量 channels 结构体，定义在 ide.c
 */
void filesys_init() {
	uint8_t channel_no = 0, dev_no, part_idx = 0;

	/* sb_buf用来存储从硬盘上读入的超级块 */
	struct super_block* sb_buf = (struct super_block*)sys_malloc(SECTOR_SIZE);
	if (sb_buf == NULL) {
		PANIC("alloc memory failed!");
	}

	printk("searching filesystem......\n");
	/* 遍历通道 */
	while (channel_no < channel_cnt) {
		dev_no = 0;
		/* 遍历通道里的硬盘 */
		while(dev_no < 2) {
			if (dev_no == 0) {		// 跨过裸盘hd60M.img
				dev_no++;
				continue;
			}
			struct disk* hd = &channels[channel_no].devices[dev_no];
			struct partition* part = hd->prim_parts;	// 主分区数组
			/* 遍历硬盘里的分区 */
			while(part_idx < 12) {						// 4个主分区+8个逻辑
				if (part_idx == 4) {
					part = hd->logic_parts;				// 逻辑分区数组
				}

				/* channels数组是全局变量,默认值为0
				 * 固 channels.disk.partition.sec_cnt 在未初始化时，也为0 */
				if (part->sec_cnt != 0) {	// 如果分区存在，以sec_cnt是否为0当判断依据
					memset(sb_buf, 0, SECTOR_SIZE);

					/* 读出分区的超级块，根据魔数判断是否存在我们定义的文件系统  */
					ide_read(hd, part->start_lba + 1, sb_buf, 1);
					if (sb_buf->magic == 0x19590318) {
						printk("%s has filesystem\n", part->name);
					} else {
						// 不存在我们的文件系统，则进行格式化
						printk("formatting %s`s partition %s......\n", hd->name, part->name);
						partition_format(part);
					}
				}
				part_idx++;
				part++;
			}	/* end of 遍历分区 */
			dev_no++;
		}	/* end of 遍历硬盘 */
		channel_no++;
	}	/* end of 遍历通道 */
	sys_free(sb_buf);


	/* 挂载分区 */
	/* 默认操作的分区 */
	char default_part[8] = "sdb1";
	/*
	struct list_elem* list_traversal(struct list* plist, function func, int arg)
	功能：遍历 plist中的每个元素elem，arg用来判断elem是否符合条件.
	回调函数 func(elem, arg)
	找到符合条件的元素返回元素指针,否则返回NULL.
	参数：
	partition_list 是所有分区的链表
	mount_partition 是挂载分区的函数
	(int)default_part 将数组地址转换成整型作为 mount_partition 的参数
	*/
	list_traversal(&partition_list, mount_partition, (int)default_part);
}

