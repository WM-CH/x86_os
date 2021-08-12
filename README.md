# x86_os

learn 《真象还原》

&nbsp;

kernel和lib/kernel目录的文件，需要在ubuntu上编译，生成bin文件。

build_kernel.sh同时编译lib/kernel和kernel目录的文件，生成kenerl.bin。

&nbsp;

逻辑扇区号：mbr=0, loader=2, kenerl=9

&nbsp;

### 编译选项

gcc -m32 使用64位编译器生成32位elf

ld的时候加上 -m elf_i386

&nbsp;

-fno-builtin 不使用C库函数

也可以-fno-builtin-function后边跟某一单个不使用的C库函数

&nbsp;

-fno-stack-protector

编译源码到目标文件时，一定要加“-fno-stack-protector”，不然默认会调函数“__stack_chk_fail”进行栈相关检查，然而是手动裸ld去链接，没有链接到“__stack_chk_fail”所在库文件，所以在链接过程一定会报错: undefined reference to `__stack_chk_fail'。解决办法不是在链接过程中，而是在编译时加此参数，强制gcc不进行栈检查，从而解决。

此外，ld 的时候加上参数"-e main"就可以了，意思是将main函数作为程序入口，ld 的默认程序入口为_start。

&nbsp;

-w的意思是关闭编译时的警告，也就是编译后不显示任何warning，因为有时在编译之后编译器会显示一些例如数据转换之类的警告，这些警告是我们平时可以忽略的。
-Wall选项意思是编译后显示所有警告。

-W选项类似-Wall，会显示警告，但是只显示编译器认为会出现错误的警告。

在编译一些项目的时候可以-W和-Wall选项一起使用。

&nbsp;

-Wstrict-prototypes 函数声明中必须有参数类型。

&nbsp;

-Wmissing-prototypes 选项要求函数必须有声明，否则编译时发出警告。 

&nbsp;

-Wsystem-headers

Print warning messages for constructs found in system header files. **Warnings from system headers are normally suppressed**, on the assumption that they usually do not indicate real problems and would only make the compiler output harder to read.

说实话，还是没明白这个。