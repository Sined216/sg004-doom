/* SG-004A Doom: app window 0x08005800-0x0806F800 (0x6A000 bytes); ld refuses anything larger.
   RAM: 96 KB (measured on the device). */
MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08005800, LENGTH = 0x6A000
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 96K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
_stack_size = 4K;

ENTRY(Reset_Handler)

SECTIONS
{
  .isr_vector : { KEEP(*(.isr_vector)) } > FLASH

  .text : {
    *(.text*)
    *(.rodata*)
    . = ALIGN(4);
    _etext = .;
  } > FLASH

  .data : {
    _sdata = .;
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } > RAM AT > FLASH
  _sidata = LOADADDR(.data);

  .bss (NOLOAD) : {
    _sbss = .;
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
  } > RAM

  /* heap runs from _ebss up to the reserved stack */
  _heap_end = _estack - _stack_size;
  ASSERT(_ebss <= _heap_end, "RAM overflow: .data + .bss do not leave room for the stack")

  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.attributes) *(.comment) }
}
