// Format of an ELF executable file

#define ELF_MAGIC 0x464C457FU  // "\x7FELF" in little endian

// File header
struct elfhdr {
  uint magic;  // must equal ELF_MAGIC
  uchar elf[12];
  ushort type; // 文件类型
  ushort machine; // 目标机器架构（如 EM_RISCV 表示 RISC-V 架构)
  uint version; // ELF 版本号
  uint64 entry; // 程序入口地址（exec 加载后跳转到该地址）
  uint64 phoff; // program header table 在文件中的偏移
  uint64 shoff; // section header table 在文件中的偏移
  uint flags; // 处理器特定标志（如浮点支持等）
  ushort ehsize; // ELF header 的大小（通常是 64 字节）
  ushort phentsize; // 每个 program header 的大小
  ushort phnum; // program header 的数量
  ushort shentsize; // 每个 section header 的大小
  ushort shnum; // section header 的数量
  ushort shstrndx; // section 名称字符串表所在的索引
};

// Program section header
struct proghdr {
  uint32 type;
  uint32 flags;
  uint64 off;
  uint64 vaddr;
  uint64 paddr;
  uint64 filesz;
  uint64 memsz;
  uint64 align;
};

// Values for Proghdr type
// 包含.text、.data
#define ELF_PROG_LOAD           1

// Flag bits for Proghdr flags
#define ELF_PROG_FLAG_EXEC      1
#define ELF_PROG_FLAG_WRITE     2
#define ELF_PROG_FLAG_READ      4
