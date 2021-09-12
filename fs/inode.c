#include "inode.h"
#include "fs.h"
#include "file.h"
#include "global.h"
#include "debug.h"
#include "memory.h"
#include "interrupt.h"
#include "list.h"
#include "stdio-kernel.h"
#include "string.h"
#include "super_block.h"

/* 用来存储inode位置 */
struct inode_position {
	bool	 two_sec;	// inode是否跨扇区
	uint32_t sec_lba;	// inode所在的扇区号
	uint32_t off_size;	// inode在扇区内的字节偏移量
};

/* 获取inode所在的扇区和扇区内的偏移量
 * out: inode_pos
 */
static void inode_locate(struct partition* part, uint32_t inode_no, struct inode_position* inode_pos) {
	/* inode_table在硬盘上是连续的 */
	ASSERT(inode_no < 4096);
	uint32_t inode_table_lba = part->sb->inode_table_lba;

	uint32_t inode_size = sizeof(struct inode);
	uint32_t off_size = inode_no * inode_size;	// 第inode_no号inode相对于inode_table_lba的字节偏移量
	uint32_t off_sec  = off_size / 512;			// 第inode_no号inode相对于inode_table_lba的扇区偏移量
	uint32_t off_size_in_sec = off_size % 512;	// 待查找的inode所在扇区中的起始地址

	/* 判断此inode是否跨越2个扇区 */
	uint32_t left_in_sec = 512 - off_size_in_sec;
	// 若扇区内剩下的空间不足以容纳一个inode,必然是inode跨越了2个扇区
	if (left_in_sec < inode_size ) {
		inode_pos->two_sec = true;	//跨越了
	} else {
		inode_pos->two_sec = false;
	}
	inode_pos->sec_lba = inode_table_lba + off_sec;
	inode_pos->off_size = off_size_in_sec;
}

/* 功能：将inode结构写回到硬盘分区。
io_buf是用于硬盘io的缓冲区，它由主调函数提供！
原因是
一般情况下把内存中的数据同步到硬盘都是最后的操作，其前已经做了大量工作，
若到这最后一步时才申请内存失败，前面的所有操作都白费了，还要回滚到之前的旧状态，代价太大
*/
void inode_sync(struct partition* part, struct inode* inode, void* io_buf) {
	uint8_t inode_no = inode->i_no;
	struct inode_position inode_pos;
	inode_locate(part, inode_no, &inode_pos);	// inode位置信息会存入inode_pos
	ASSERT(inode_pos.sec_lba <= (part->start_lba + part->sec_cnt));

	/* 硬盘中的inode中的成员inode_tag和i_open_cnts是不需要的，
	* 它们只在内存中有效，记录链表位置和被多少进程共享，
	* 将inode同步到硬盘时,清掉这三项即可。
	*/
	struct inode pure_inode;
	memcpy(&pure_inode, inode, sizeof(struct inode));
	pure_inode.i_open_cnts = 0;
	pure_inode.write_deny = false;	// 置为false,以保证在硬盘中读出时为可写
	pure_inode.inode_tag.prev = pure_inode.inode_tag.next = NULL;

	char* inode_buf = (char*)io_buf;
	// 若inode结构，跨了两个扇区,就要读出两个扇区再写入两个扇区
	if (inode_pos.two_sec) {
		/* 读写硬盘是以扇区为单位的
		所以需要将待写入的inode结构拼入到这2个扇区的中间位置 */
		ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 2);	// inode_table在格式化时，写入硬盘是连续写入的，所以读入2块扇区
		memcpy((inode_buf + inode_pos.off_size), &pure_inode, sizeof(struct inode));	// inode_buf+inode在扇区中的偏移
		ide_write(part->my_disk, inode_pos.sec_lba, inode_buf, 2);
	} else {
		// 若inode结构，只在一个扇区中
		ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 1);
		memcpy((inode_buf + inode_pos.off_size), &pure_inode, sizeof(struct inode));	// inode_buf+inode在扇区中的偏移
		ide_write(part->my_disk, inode_pos.sec_lba, inode_buf, 1);
	}
}

/* 根据i结点号，获取到相应的inode结构，插入到inode缓存队列，并返回它 */
struct inode* inode_open(struct partition* part, uint32_t inode_no) {
	/* 1.先在已打开inode链表中找inode，此链表作用是提速 */
	struct list_elem* elem = part->open_inodes.head.next;
	struct inode* inode_found;
	while (elem != &part->open_inodes.tail) {
		inode_found = elem2entry(struct inode, inode_tag, elem);
		if (inode_found->i_no == inode_no) {
			inode_found->i_open_cnts++;
			return inode_found;
		}
		elem = elem->next;
	}

	
	/* 2.open_inodes链表中找不到，从硬盘上读入此inode并加入到inode缓存链表 */
	struct inode_position inode_pos;	// 记录inode位置的结构
	inode_locate(part, inode_no, &inode_pos);

	/* 为使通过sys_malloc创建的新inode被所有任务共享，需要将inode置于内核空间
	故临时将cur_pbc->pgdir置为NULL，sys_malloc就会以为是从内核空间分配 */
	struct task_struct* cur = running_thread();
	uint32_t* cur_pagedir_bak = cur->pgdir;
	cur->pgdir = NULL;
	inode_found = (struct inode*)sys_malloc(sizeof(struct inode));
	cur->pgdir = cur_pagedir_bak;	// 恢复pgdir

	char* inode_buf;
	if (inode_pos.two_sec) {	// inode结构跨扇区了
		inode_buf = (char*)sys_malloc(1024);
		// inode_table在格式化时，写入硬盘是连续写入的，所以可以连续读入2块扇区
		ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 2);
	} else {
		inode_buf = (char*)sys_malloc(512);
		ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 1);
	}
	memcpy(inode_found, inode_buf + inode_pos.off_size, sizeof(struct inode));	// inode_buf+inode在扇区中的偏移

	
	/* 3.马上要用这个inode，固插入到队首 */
	list_push(&part->open_inodes, &inode_found->inode_tag);
	inode_found->i_open_cnts = 1;

	sys_free(inode_buf);
	return inode_found;
}

/* 关闭inode或减少inode的打开数 */
//1.从inode队列踢掉 2.释放内核内存池中的这个inode内存
void inode_close(struct inode* inode) {
	/* 若没有进程再打开此文件,将此inode去掉并释放空间 */
	enum intr_status old_status = intr_disable();
	if (--inode->i_open_cnts == 0) {
		list_remove(&inode->inode_tag);
		/* inode结构在内核空间，释放时要释放到内核内存池。
		pgdir为NULL时，sys_free认为是内核空间 */
		struct task_struct* cur = running_thread();
		uint32_t* cur_pagedir_bak = cur->pgdir;	// 进程自己页目录表的虚拟地址，加载到cr3时需转成物理地址
		cur->pgdir = NULL;
		sys_free(inode);
		cur->pgdir = cur_pagedir_bak;
	}
	intr_set_status(old_status);
}

/* 初始化一个inode结构，inode号是inode_no */
void inode_init(uint32_t inode_no, struct inode* new_inode) {
	new_inode->i_no = inode_no;
	new_inode->i_size = 0;
	new_inode->i_open_cnts = 0;
	new_inode->write_deny = false;

	/* 初始化块索引数组i_sector */
	uint8_t sec_idx = 0;
	while (sec_idx < 13) {
		/* i_sectors[12]为一级间接块地址 */
		new_inode->i_sectors[sec_idx] = 0;
		sec_idx++;
	}
	/*为什么不提前分配inode管理的数据块（对我们来说是块=扇区）
	1.不知道文件大小，因此不知道分配多少个扇区合适
	2.文件创建后未必马上会写数据*/
}