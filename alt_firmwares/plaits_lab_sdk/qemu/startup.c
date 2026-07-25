/* Minimal bare-metal startup for the QEMU cost harness. Not a device firmware:
   no clocks, no peripherals, no interrupts -- just enough to reach main() with
   .data and .bss correct, so the plugin's counts cover the engine and nothing
   else. SPDX-License-Identifier: MIT */
#include <stdint.h>

/* Compiled by g++ (the toolchain driver the firmware uses), so the reset entry
   point must keep its C name or the linker cannot find it. */
#ifdef __cplusplus
extern "C" {
#endif

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
extern int main(void);

void Reset_Handler(void)
{
    /* A Cortex-M4 traps floating point until CP10/CP11 are enabled, so without
       this the first FP instruction faults straight into the handler below and
       the guest hangs -- which is exactly what it did. The real firmware does
       this in its own startup; a bare-metal harness has to do it too. */
    *(volatile uint32_t *)0xE000ED88 |= (0xFu << 20);

    uint32_t *src = &_sidata, *dst = &_sdata;
    while (dst < &_edata) *dst++ = *src++;
    for (dst = &_sbss; dst < &_ebss; ) *dst++ = 0;
    main();
    for (;;) { }
}

/* Referenced by C++ static-object teardown, which never runs here. */
void* __dso_handle = 0;

static void Default_Handler(void) { for (;;) { } }

__attribute__((section(".isr_vector"), used))
void (* const g_vectors[])(void) = {
    (void (*)(void))&_estack,
    Reset_Handler,
    Default_Handler,  /* NMI        */
    Default_Handler,  /* HardFault  */
};

#ifdef __cplusplus
}
#endif
