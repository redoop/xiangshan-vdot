OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS {
  . = 0x80000000;
  .text : { *(.text.startup) *(.text*) }
  . = ALIGN(16);
  .data : { *(.data*) }
  .bss : { *(.bss*) }
}
