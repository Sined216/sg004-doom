#include <stdint.h>
#include "board.h"

extern uint32_t _estack, _sidata, _sdata, _edata, _sbss, _ebss;
int main(void);
void reset_main(void);
void SysTick_Handler(void);
void fault_screen(const uint32_t *frame, uint32_t exc_return);

/* The FNIRSI bootloader jumps here without loading MSP from the vector table (it was seen at 0x20000E68,
   inside .bss), so the stack pointer has to be set explicitly before any C code runs. */
__attribute__((naked)) void Reset_Handler(void)
{
    __asm volatile(
        "ldr   r0, =_estack        \n"
        "msr   msp, r0             \n"
        "b.w   reset_main          \n"
        ".ltorg                    \n");
}

void reset_main(void)
{
    uint32_t *src = &_sidata, *dst = &_sdata;
    while (dst < &_edata) *dst++ = *src++;
    for (dst = &_sbss; dst < &_ebss; ) *dst++ = 0;
    SCB_VTOR = 0x08005800u;
    main();
    for (;;) {}
}

/* Any fault or stray interrupt: show the fault screen (fault.c), which keeps the power key working,
   otherwise the device would be stuck on with PB7 holding the power latch. */
__attribute__((naked)) void Default_Handler(void)
{
    __asm volatile(
        "tst   lr, #4              \n"
        "ite   eq                  \n"
        "mrseq r0, msp             \n"
        "mrsne r0, psp             \n"
        "mov   r1, lr              \n"
        "b.w   fault_screen        \n");
}

#pragma GCC diagnostic ignored "-Woverride-init"   /* range default, then specific handlers */
__attribute__((section(".isr_vector"), used))
const void *const vector_table[16 + 68] = {
    [0] = &_estack,
    [1] = Reset_Handler,
    [2 ... 16 + 67] = Default_Handler,
    [15] = SysTick_Handler,
};
