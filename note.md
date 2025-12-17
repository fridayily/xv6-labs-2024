riscv64-unknown-elf-readelf -S ./kernel/kernel
- 查看所有节信息

riscv64-unknown-elf-objdump --dwarf=decodedline ./kernel/kernel
- 查看解码后的行号表