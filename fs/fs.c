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
#include "file.h"
#include "console.h"

struct partition* cur_part;	 // 默认操作的分区

/*
挂载分区：
把该分区文件系统的元信息从硬盘上读出来加载到内存中，
这样硬盘资源的变化都用内存中元信息来跟踪，
如果有写操作，及时将内存中的元信息同步写入到硬盘以持久化。
*/
//sb_buf没释放！
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


/* 将最上层路径名称解析出来，比如pathname="a/b/c"得到name_store="a"
 * 返回剩下的目录 "b/c" */
static char* path_parse(char* pathname, char* name_store) {
	if (pathname[0] == '/') {	// 根目录不需要单独解析
		/* 路径中出现1个或多个连续的字符'/',将这些'/'跳过,如"///a/b" */
		while(*(++pathname) == '/');
	}

	/* 开始一般的路径解析 */
	while (*pathname != '/' && *pathname != 0) {
		*name_store++ = *pathname++;
	}

	if (pathname[0] == 0) {   // 若路径字符串为空则返回NULL
		return NULL;
	}
	return pathname;
}

/* 返回路径深度,比如/a/b/c,深度为3 */
int32_t path_depth_cnt(char* pathname) {
	ASSERT(pathname != NULL);
	char* p = pathname;
	char name[MAX_FILE_NAME_LEN];	// 用于path_parse的参数做路径解析
	uint32_t depth = 0;

	/* 解析路径,从中拆分出各级名称 */
	p = path_parse(p, name);
	while (name[0]) {
		depth++;
		memset(name, 0, MAX_FILE_NAME_LEN);
		if (p) {	// 如果p不等于NULL,继续分析路径
			p  = path_parse(p, name);
		}
	}
	return depth;
}

/* 搜索文件pathname，若找到则返回其inode号，相应信息填入path_search_record结构【此结构由主调函数提供，也由主调函数释放】
 * 否则返回-1
 * 全局变量 struct dir root_dir 定义在 dir.c
 * 也支持 /./a 或者 /../b 会把.和..当做一个目录项继续往下层找
 * /a/b/c若c不存在，那么 searched_record.searched_path 是 /a/b/c
 * 注意下边三种return的地方，是三种情况！
 */
static int search_file(const char* pathname, struct path_search_record* searched_record) {
	/* 如果待查找的是根目录,为避免下面无用的查找,直接返回已知根目录信息 */
	if (!strcmp(pathname, "/") || !strcmp(pathname, "/.") || !strcmp(pathname, "/..")) {
		searched_record->parent_dir = &root_dir;	// 直接父目录
		searched_record->file_type = FT_DIRECTORY;	// FT_UNKNOWN代表没找到，找到才有类型是文件或目录
		searched_record->searched_path[0] = 0;		// 搜索过的路径置空
		return 0;
	}

	uint32_t path_len = strlen(pathname);
	/* 保证pathname至少是这样的路径/x且小于最大长度 */
	ASSERT(pathname[0] == '/' && path_len > 1 && path_len < MAX_PATH_LEN);
	char* sub_path = (char*)pathname;
	struct dir* parent_dir = &root_dir;
	struct dir_entry dir_e;

	/* 数组name每次的值分别是各级目录的名字："a","b","c" */
	char name[MAX_FILE_NAME_LEN] = {0};

	searched_record->parent_dir = parent_dir;	// 直接父目录
	searched_record->file_type = FT_UNKNOWN;	// 初始值代表没找到，找到才有类型是文件或目录
	uint32_t parent_inode_no = 0;  // 备份各层解析出来的路径，的父目录，的inode号

	sub_path = path_parse(sub_path, name);
	while (name[0]) {	   // 若第一个字符就是结束符,结束循环
		/* 记录查找过的路径,但不能超过searched_path的长度512字节 */
		ASSERT(strlen(searched_record->searched_path) < 512);

		/* 追加到已存在的父目录 */
		strcat(searched_record->searched_path, "/");
		strcat(searched_record->searched_path, name);

		/* 在所给的目录中查找目录项，找到后将目录项填入dir_e */
		if (false != search_dir_entry(cur_part, parent_dir, name, &dir_e)) {
			memset(name, 0, MAX_FILE_NAME_LEN);
			/* 若sub_path不等于NULL，也就是搜索未结束，继续拆分路径 */
			if (sub_path) {
				sub_path = path_parse(sub_path, name);
			}

			if (FT_DIRECTORY == dir_e.f_type) {			// 如果被打开的是目录
				parent_inode_no = parent_dir->inode->i_no;
				dir_close(parent_dir);

				parent_dir = dir_open(cur_part, dir_e.i_no);
				searched_record->parent_dir = parent_dir;	// 更新直接父目录
				continue;
			} else if (FT_REGULAR == dir_e.f_type) {	// 若是普通文件
				/* 如果搜索路径为 /a/b 但是目录下只有文件a 此时也会返回文件a的inode号【中间某个目录不存在】
				 * 由主调函数根据 searched_record.searched_path 判断："提供的 pathname 是否正确，是否处理完了"
				 * 【情况1】
				 */
				searched_record->file_type = FT_REGULAR;
				return dir_e.i_no;
			}
		} else {	//若找不到,则返回-1
			/* 找不到目录项时，要留着 parent_dir 不要关闭，
			 * 主调函数需要据此知道在哪个目录中创建文件，此时的 searched_record->parent_dir 指向父目录，主调函数负责关闭该目录
			 * 主调函数，创建新文件的话需要在 parent_dir 中创建
			 * 【情况2】
			 */
			return -1;
		}
	}

	/* 执行到此，必然是遍历了完整路径，并且查找的名字，只找到了一个目录【最后一级路径是目录】
	 * 待查找的目标是目录，如“/a/b/c”， c 是目录，不是普通文件。
	 * 此时 searched_record-> parent_dir 是路径 pathname 中的最后一级目录 c，并不是倒数第二级的父目录 b
	 * 要保证，无论搜索目标是普通文件，还是目录，searched_record->parent_dir 中记录的都应该是父目录！
	 * 因此要关闭目录c，重新打开目录b
	 * 【情况3】
	 */
	dir_close(searched_record->parent_dir);
	searched_record->parent_dir = dir_open(cur_part, parent_inode_no);
	searched_record->file_type = FT_DIRECTORY;
	return dir_e.i_no;	//目录c的inode号
}

/* 打开或创建文件成功后,返回文件描述符,否则返回-1
 * 对应 file.c 中的 file_create
 *
 * open(const char * pathname, (O_CREAT|O_WRONLY|O_TRUNC));
 */
int32_t sys_open(const char* pathname, uint8_t flags) {
	/* 对目录要用dir_open,这里只有open文件 */
	if (pathname[strlen(pathname) - 1] == '/') {
		printk("can`t open a directory %s\n", pathname);
		return -1;
	}
	ASSERT(flags <= 7);
	int32_t fd = -1;	// 默认为找不到

	struct path_search_record searched_record;
	memset(&searched_record, 0, sizeof(struct path_search_record));

	/* 记录目录深度.帮助判断中间某个目录不存在的情况 */
	uint32_t pathname_depth = path_depth_cnt((char*)pathname);

	/* 先检查文件是否存在 */
	int inode_no = search_file(pathname, &searched_record);
	bool found = (inode_no != -1 ? true : false);

	if (searched_record.file_type == FT_DIRECTORY) {
		printk("can`t open a direcotry with open(), use opendir() to instead\n");
		dir_close(searched_record.parent_dir);	//主调函数负责关闭该目录
		return -1;
	}

	uint32_t path_searched_depth = path_depth_cnt(searched_record.searched_path);

	/* 先判断是否把pathname的各层目录都访问到了 */
	// 【search_file情况1】说明并没有访问到全部的路径,某个中间目录是不存在的【是一个同名的普通文件】
	if (pathname_depth != path_searched_depth) {
		printk("cannot access %s: Not a directory, subpath %s is`t exist\n",
			pathname, searched_record.searched_path);
		dir_close(searched_record.parent_dir);
		return -1;
	}

	// 【search_file情况2】若是在最后一个路径上没找到
	// 并且此时也不是要创建文件,直接返回-1 */
	if (!found && !(flags & O_CREAT)) {
		printk("in path %s, file %s is`t exist\n",
			searched_record.searched_path, (strrchr(searched_record.searched_path, '/') + 1));
		dir_close(searched_record.parent_dir);
		return -1;
	} else if (found && flags & O_CREAT) {	// 若要创建的文件已存在，相同目录下不可以有同名文件，返回-1
		printk("%s has already exist!\n", pathname);
		dir_close(searched_record.parent_dir);
		return -1;
	}

	switch (flags & O_CREAT) {	// sys_open("xxx", O_CREAT|O_XXX)
		case O_CREAT:
		printk("creating file\n");
		// 【search_file情况3】主调函数用到此目录，在该目录下创建文件
		fd = file_create(searched_record.parent_dir, (strrchr(pathname, '/') + 1), flags);
		dir_close(searched_record.parent_dir);
		break;

		default:
		/* 其余情况均为打开已存在文件: O_RDONLY,O_WRONLY,O_RDWR */
		fd = file_open(inode_no, flags);
	}

	/* 此fd是指任务pcb->fd_table数组中的元素下标,
	 * 并不是指全局file_table中的下标 */
	return fd;
}

/* 将文件描述符转化为文件表的下标 */
static uint32_t fd_local2global(uint32_t local_fd) {
	struct task_struct* cur = running_thread();
	int32_t global_fd = cur->fd_table[local_fd];
	ASSERT(global_fd >= 0 && global_fd < MAX_FILE_OPEN);
	return (uint32_t)global_fd;
}

/* 关闭文件描述符fd指向的文件,成功返回0,否则返回-1 */
int32_t sys_close(int32_t fd) {
	int32_t ret = -1;   // 返回值默认为-1,即失败
	if (fd > 2) {
		uint32_t _fd = fd_local2global(fd);
		ret = file_close(&file_table[_fd]);
		running_thread()->fd_table[fd] = -1; // 使该文件描述符位可用
	}
	return ret;
}

/* 将buf中连续count个字节写入文件描述符fd,成功则返回写入的字节数,失败返回-1 */
int32_t sys_write(int32_t fd, const void* buf, uint32_t count) {
   if (fd < 0) {
      printk("sys_write: fd error\n");
      return -1;
   }
   if (fd == stdout_no) {
      char tmp_buf[1024] = {0};
      memcpy(tmp_buf, buf, count);
      console_put_str(tmp_buf);
      return count;
   }
   uint32_t _fd = fd_local2global(fd);
   struct file* wr_file = &file_table[_fd];
   if (wr_file->fd_flag & O_WRONLY || wr_file->fd_flag & O_RDWR) {
      uint32_t bytes_written  = file_write(wr_file, buf, count);
      return bytes_written;
   } else {
      console_put_str("sys_write: not allowed to write file without flag O_RDWR or O_WRONLY\n");
      return -1;
   }
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


	/* 将当前分区的根目录打开 + 初始化文件表 */
	open_root_dir(cur_part);
	uint32_t fd_idx = 0;
	while (fd_idx < MAX_FILE_OPEN) {
		file_table[fd_idx++].fd_inode = NULL;
	}
}

