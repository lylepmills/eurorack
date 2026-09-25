/* Minimal bare-metal startup for the QEMU cost harness. Not a device firmware:
   no clocks, no peripherals, no interrupts -- just enough to reach main() with
   .data, .bss and static constructors done, so the plugin's counts cover the
   engine and nothing else. SPDX-License-Identifier: MIT */
#include <stdint.h>

/* Compiled by g++ (the toolchain driver the firmware uses), so the reset entry
   point must keep its C name or the linker cannot find it. */
#ifdef __cplusplus
extern "C" {
#endif

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
extern void (*__init_array_start[])(void);
extern void (*__init_array_end[])(void);
extern int main(void);

void Reset_Handler(void)
{
    /* A Cortex-M4 traps floating point until CP10/CP11 are enabled, so without
       this the first FP instruction faults straight into the handler below and
       the guest hangs -- which is exactly what it did. The real firmware does
       this in its own startup; a bare-metal harness has to do it too. */
    *(volatile uint32_t *)0xE000ED88 |= (0xFu << 20);

    uint32_t *src = &_sidata, *dst = &_sdata;
    void (**ctor)(void);
    while (dst < &_edata) *dst++ = *src++;
    for (dst = &_sbss; dst < &_ebss; ) *dst++ = 0;
    /* Static constructors, as the firmware's startup runs them through
       __libc_init_array. Skipping them left dynamically initialized tables
       zero: Chords' factory wave line was all NULL, so its wavetable voices
       read the vector table and the first bytes of .text as waveform data,
       and the output changed with every unrelated code change. */
    for (ctor = __init_array_start; ctor < __init_array_end; ++ctor) (*ctor)();
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
