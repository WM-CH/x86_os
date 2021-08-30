#ifndef __FS_DIR_H
#define __FS_DIR_H
#include "stdint.h"
#include "fs.h"
#include "inode.h"
#include "ide.h"
#include "global.h"

#define MAX_FILE_NAME_LEN	16	// 最大文件名长度

/* 目录结构 */
struct dir {
	struct inode* inode;		// 该 inode 必然是在“已打开的 inode 队列”
	uint32_t dir_pos;			// 记录遍历目录时，"游标"在目录内的偏移
	uint8_t dir_buf[512];		// 目录的数据缓存，读取目录时，用来存储返回的目录项
};

/* 目录项结构 */
struct dir_entry {
	char filename[MAX_FILE_NAME_LEN];	// 普通文件或目录名称
	uint32_t i_no;						// 普通文件或目录对应的inode编号
	enum file_types f_type;				// 文件类型
};

#endif
