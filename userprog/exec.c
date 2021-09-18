#include "exec.h"
#include "thread.h"    
#include "stdio-kernel.h"
#include "fs.h"
#include "string.h"
#include "global.h"
#include "memory.h"

extern void intr_exit(void);
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* 32位elf头 */
struct Elf32_Ehdr {
	unsigned char e_ident[16];
	Elf32_Half    e_type;		// 32 or 64 ELF
	Elf32_Half    e_machine;	// x86
	Elf32_Word    e_version;
	Elf32_Addr    e_entry;		// 入口地址（虚拟地址）
	Elf32_Off     e_phoff;		// 程序头在elf文件中的起始地址
	Elf32_Off     e_shoff;
	Elf32_Word    e_flags;
	Elf32_Half    e_ehsize;
	Elf32_Half    e_phentsize;
	Elf32_Half    e_phnum;		// 程序头表中条目的数量，也就是段的个数
	Elf32_Half    e_shentsize;	// 程序头表中每个条目的字节大小，即 描述段的数据结构的大小 == sizeof(struct Elf32_Phdr)
	Elf32_Half    e_shnum;
	Elf32_Half    e_shstrndx;
};

/* 程序头表Program header.就是段segment描述头 */
struct Elf32_Phdr {
	Elf32_Word p_type;		// 见下面的enum segment_type
	Elf32_Off  p_offset;	// 在elf文件中，segment的偏移地址
	Elf32_Addr p_vaddr;		// 在内存中，希望被加载到的地址
	Elf32_Addr p_paddr;
	Elf32_Word p_filesz;	// Segment的大小
	Elf32_Word p_memsz;		// 因为BSS段，所以 memsz 不等于 filesz
	Elf32_Word p_flags;
	Elf32_Word p_align;
};

/* 段类型 */
enum segment_type {
	PT_NULL,		// 忽略
	PT_LOAD,		// 可加载程序段，只关注这一种，可加载的段，即程序体
	PT_DYNAMIC,		// 动态加载信息 
	PT_INTERP,		// 动态加载器名称
	PT_NOTE,		// 一些辅助信息
	PT_SHLIB,		// 保留
	PT_PHDR			// 程序头表
};

/*
参数：文件描述符 fd、段在文件中的字节偏移量 offset、段大小 filesz、段被加载到的虚拟地址 vaddr
功能：将文件描述符 fd 指向的文件中，偏移为 offset，大小为 filesz 的一个"段"，加载到虚拟地址为 vaddr 的内存空间。*/
static bool segment_load(int32_t fd, uint32_t offset, uint32_t filesz, uint32_t vaddr) {
	// vaddr地址所在的页的指针（指针的地址是虚拟空间的地址）（指向的空间包含:页的物理地址和属性）指针
	uint32_t vaddr_first_page = vaddr & 0xfffff000;
	uint32_t size_in_first_page = PG_SIZE - (vaddr & 0x00000fff);	// 第一个页中可以用的空间
	uint32_t occupy_pages = 0;
	/* 若一个页框容不下该段 */
	if (filesz > size_in_first_page) {
		uint32_t left_size = filesz - size_in_first_page;
		occupy_pages = DIV_ROUND_UP(left_size, PG_SIZE) + 1;		// 1是指vaddr_first_page
	} else {
		occupy_pages = 1;
	}

	/* 为进程分配内存 */
	uint32_t page_idx = 0;
	uint32_t vaddr_page = vaddr_first_page;
	while (page_idx < occupy_pages) {
		uint32_t* pde = pde_ptr(vaddr_page);	//虚拟地址，指向 页目录项 的指针，指向的空间含：地址和属性
		uint32_t* pte = pte_ptr(vaddr_page);	//虚拟地址，指向 页表项   的指针

		/* 如果pde不存在,或者pte不存在就分配内存.
		 * pde的判断要在pte之前,否则pde若不存在会导致
		 * 判断pte时缺页异常 */
		if (!(*pde & 0x00000001) || !(*pte & 0x00000001)) {	//页目录项或者页表项不存在
			if (get_a_page(PF_USER, vaddr_page) == NULL) {
				return false;
			}
		} // 如果原进程的页表已经分配了,利用现有的物理页,直接覆盖进程体
		vaddr_page += PG_SIZE;	//下一页
		page_idx++;
	}
	sys_lseek(fd, offset, SEEK_SET);
	sys_read(fd, (void*)vaddr, filesz);	//读到vaddr处
	return true;
}

/* 从文件系统上加载用户程序pathname
 * 成功则返回程序的起始地址，否则返回-1 */
static int32_t load(const char* pathname) {
	int32_t ret = -1;
	struct Elf32_Ehdr elf_header;
	struct Elf32_Phdr prog_header;
	memset(&elf_header, 0, sizeof(struct Elf32_Ehdr));

	int32_t fd = sys_open(pathname, O_RDONLY);
	if (fd == -1) {
		return -1;
	}

	if (sys_read(fd, &elf_header, sizeof(struct Elf32_Ehdr)) != sizeof(struct Elf32_Ehdr)) {
		ret = -1;
		goto done;
	}

	/* 校验32位的elf头 */
	/* 
	e_phnum == 程序头表中条目的数量，也就是段的个数
	e_phentsize == 程序头表中每个条目的字节大小，即 描述段的数据结构的大小 == sizeof(struct Elf32_Phdr)
	细节见readme */
	if (memcmp(elf_header.e_ident, "\177ELF\1\1\1", 7) \
			|| elf_header.e_type != 2 \
			|| elf_header.e_machine != 3 \
			|| elf_header.e_version != 1 \
			|| elf_header.e_phnum > 1024 \
			|| elf_header.e_phentsize != sizeof(struct Elf32_Phdr)) {
		ret = -1;
		goto done;
	}

	Elf32_Off prog_header_offset = elf_header.e_phoff;		// 程序头的起始地址
	Elf32_Half prog_header_size = elf_header.e_phentsize;	// 描述段的数据结构的大小 == sizeof(struct Elf32_Phdr)

	/* 遍历所有程序头 */
	uint32_t prog_idx = 0;
	while (prog_idx < elf_header.e_phnum) {	//段的个数
		memset(&prog_header, 0, prog_header_size);

		/* 将文件的指针定位到程序头 */
		sys_lseek(fd, prog_header_offset, SEEK_SET);

		/* 只获取程序头 */
		if (sys_read(fd, &prog_header, prog_header_size) != prog_header_size) {
			ret = -1;
			goto done;
		}

		/* 如果是可加载段就调用 segment_load 加载到内存 */
		if (PT_LOAD == prog_header.p_type) {
			if (!segment_load(fd, prog_header.p_offset, prog_header.p_filesz, prog_header.p_vaddr)) {
				ret = -1;
				goto done;
			}
		}

		/* 更新下一个程序头的偏移 */
		prog_header_offset += elf_header.e_phentsize;
		prog_idx++;
	}
	ret = elf_header.e_entry;
done:
	sys_close(fd);
	return ret;
}

/* 用path指向的程序替换当前进程 */
int32_t sys_execv(const char* path, const char* argv[]) {
	uint32_t argc = 0;
	while (argv[argc]) {
		argc++;
	}
	// 以前是用汇编加载的elf格式的内核，现在使用C语言方式加载elf格式的应用程序。
	int32_t entry_point = load(path);     
	if (entry_point == -1) {	 // 若加载失败则返回-1
		return -1;
	}

	struct task_struct* cur = running_thread();
	/* 修改进程名 */
	memcpy(cur->name, path, TASK_NAME_LEN);
	cur->name[TASK_NAME_LEN-1] = 0;

	struct intr_stack* intr_0_stack = (struct intr_stack*)((uint32_t)cur + PG_SIZE - sizeof(struct intr_stack));
	/* 参数传递给用户进程 */
	/* 新进程从 intr_exit 返回后是第一次运行，因此运行之初通用寄存器中的值都是无效的，只有运行之后寄存器中的值才是有意义的，
	故 argc 和 argv 其实放在哪两个通用寄存器中都可以，自己定。
	习惯用法：
	ebx 经常做基址寄存器，argv 本来就是所有参数的基地址，
	ecx 经常做循环控制次数寄存器，argc 本来就是 argv 的参数个数，也就是循环次数
	现在把参数放在哪个寄存器中，将来在获取参数时就从哪些寄存器中取，
	将来实现简易版 c 运行库，那会涉及到从寄存器中获取参数。*/
	intr_0_stack->ebx = (int32_t)argv;
	intr_0_stack->ecx = argc;
	intr_0_stack->eip = (void*)entry_point;
	/* 使新用户进程的栈地址为最高用户空间地址 */
	/* 1. 老进程用户栈中的数据只适用于老进程，对新进程没用，故新进程的用户栈应该从新开始。
	   2. 为了后续传入参数做准备：用户空间的最高处用于存储命令行参数。*/
	intr_0_stack->esp = (void*)0xc0000000;

	/* exec不同于fork,为使新进程更快被执行,直接从中断返回 */
	/* 将新进程内核栈地址赋值给 esp 寄存器，然后跳转到 intr_exit，假装从中断返回，实现了新进程的运行。 */
	asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (intr_0_stack) : "memory");

	//并不会从这里返回
	return 0;
}
