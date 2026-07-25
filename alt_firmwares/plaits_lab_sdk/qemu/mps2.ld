/* MPS2-AN386 (Cortex-M4) memory map, arranged to mirror the STM32F373's split:
   code and constant tables in the low region (flash on the real device), stack
   and mutable state at 0x20000000 (SRAM on both). The QEMU plugin classifies
   reads by that boundary, so this layout is what makes "flash read" meaningful. */
MEMORY
{
  CODE (rx)  : ORIGIN = 0x00000000, LENGTH = 4M
  RAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 4M
}

ENTRY(Reset_Handler)

SECTIONS
{
  .isr_vector : { KEEP(*(.isr_vector)) } > CODE
  .text   : { *(.text*) *(.glue_7*) } > CODE
  .rodata : { *(.rodata*) } > CODE
  . = ALIGN(4);
  _sidata = .;

  .data : AT (_sidata) { _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } > RAM
  .bss  : { _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } > RAM

  /* newlib's _sbrk wants a heap base even though nothing here allocates. */
  . = ALIGN(8);
  PROVIDE(end = .);
  PROVIDE(_end = .);

  _estack = ORIGIN(RAM) + LENGTH(RAM) - 16;
  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) }
}
