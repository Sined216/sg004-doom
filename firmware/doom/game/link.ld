/* SG-004A Doom: app window 0x08005800-0x0806F800 (0x6A000 bytes); ld refuses anything larger.
   RAM: 96 KB (measured on the device). Layout follows the DMC100 firmware's regions.ld/doom.ld, but the stack is at
   the top, the code is resident (no overlays) and the frame region sits behind our own .data/.bss.

     0x20000000  .data, .bss (the fixed core, whatever size it turns out to be)
     FRAME_BASE  frame region of the map's plan (16 B aligned): strip buffers, pass-1 lists, pools, strip arrays,
                 then the caches and the level arena, up to __ram_end. The sizes are the planner's budgets for
                 the converter's ram_layout (tools/conv/ram_layout_default.json): 25,504 B behind the core. The
                 tic scratch dmc_tic is fixed at FRAME_BASE + 6,912.
     top         stack, STACK_SIZE bytes below _estack (the FNIRSI bootloader does not load MSP, startup does) */

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08005800, LENGTH = 0x6A000
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 96K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
STACK_SIZE = 8K;

/* per-plan sizes of regions.ld (plan D, plan A) and the budget behind the core */
FRAME_MIN = 8368;
FRAME_MAX = 15208;
FRAME_BUDGET = 25504;
TIC_OFFSET = 6912;
TIC_SIZE = 1456;
STRIP_MAX = 6400;

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
    *(SORT_BY_ALIGNMENT(.data*))
    . = ALIGN(4);
    _edata = .;
  } > RAM AT > FLASH
  _sidata = LOADADDR(.data);

  .bss (NOLOAD) : {
    _sbss = .;
    *(SORT_BY_ALIGNMENT(.bss*))
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
  } > RAM
  __core_end = .;
  __code_end = LOADADDR(.data) + SIZEOF(.data);

  /* nothing is placed in the frame region at link time */
  .dynram (NOLOAD) : {
    . = ALIGN(16);
    __frame_start = .;
    __dyn_free = .;
  } > RAM

  __ram_end = __frame_start + FRAME_BUDGET;
  __frame_min = FRAME_MIN;
  __frame_max = FRAME_MAX;
  __core_size = __core_end - ORIGIN(RAM);
  __dmc_variant = 1;
  dmc_tic = __frame_start + TIC_OFFSET;

  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.ARM.attributes) *(.comment) }
}

ASSERT(__code_end <= 0x08030000, "the code runs into the data region of sg_layout.h (SG_INT_BASE)")
ASSERT(__ram_end + STACK_SIZE <= _estack, "RAM overflow: .data + .bss + frame region + stack do not fit")
ASSERT(TIC_OFFSET >= STRIP_MAX, "the tic scratch must not overlap the strip buffers")
ASSERT(TIC_OFFSET + TIC_SIZE <= FRAME_MIN, "the tic scratch must end in the smallest frame region")
ASSERT(__frame_start % 16 == 0, "FRAME_BASE must be 16 B aligned")
ASSERT(dmc_tic % 4 == 0, "the tic scratch must be word aligned")
