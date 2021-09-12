#ifndef __FS_INODE_H
#define __FS_INODE_H
#include "stdint.h"
#include "list.h"
#include "ide.h"

/* inode结构 */
struct inode {
	uint32_t i_no;			// inode编号

	/* 当此inode是文件时,i_size = 文件大小
	 * 当此inode是目录时,i_size = 该目录下所有目录项大小之和 */
	uint32_t i_size;

	uint32_t i_open_cnts;	// 此文件被打开的次数
	bool write_deny;		// 写文件不能并行,进程写文件时置为true

	/* i_sectors[0-11]是直接块, i_sectors[12]用来存储一级间接块指针 */
	uint32_t i_sectors[13];	// 应该叫 i_block 但是我们块大小=扇区大小，所以叫 i_sectors
	struct list_elem inode_tag;		//加入“已打开的 inode 列表”作为一个缓存方便查找已打开的文件
};

struct inode* inode_open(struct partition* part, uint32_t inode_no);
void inode_sync(struct partition* part, struct inode* inode, void* io_buf);
void inode_init(uint32_t inode_no, struct inode* new_inode);
void inode_close(struct inode* inode);
void inode_release(struct partition* part, uint32_t inode_no);
void inode_delete(struct partition* part, uint32_t inode_no, void* io_buf);
#endif
