/*
 * Minimal Cortex-M3 startup: vector table, Reset_Handler, and default
 * exception handlers. Vector table layout and required FreeRTOS handler
 * names (vPortSVCHandler/xPortPendSVHandler/xPortSysTickHandler) follow
 * FreeRTOS's own official demo for this exact QEMU machine
 * (Demo/CORTEX_MPS2_QEMU_IAR_GCC/build/gcc/startup_gcc.c in the
 * FreeRTOS/FreeRTOS repo).
 *
 * Explicitly copies .data from flash and zeroes .bss before calling
 * main(), unlike that reference (which skips both): QEMU's ELF loader
 * places .data's initial values directly at their RAM addresses and hands
 * QEMU-allocated (already-zeroed) RAM for .bss, so skipping this happens
 * to work under `qemu-system-arm -kernel`, but wouldn't be correct startup
 * on real silicon, where RAM contents at power-on are undefined.
 */
#include <stdint.h>

extern void vPortSVCHandler(void);
extern void xPortPendSVHandler(void);
extern void xPortSysTickHandler(void);

static void Default_Handler(void);
static void HardFault_Handler(void);
void Reset_Handler(void);

extern int main(void);

extern uint32_t _estack;
extern uint32_t _sidata;
extern uint32_t _sdata;
extern uint32_t _edata;
extern uint32_t _sbss;
extern uint32_t _ebss;

/* Cortex-M3 vector table. The first two entries (initial stack pointer,
   reset handler) are loaded directly by hardware on reset; everything
   after that is this project's own dispatch table. */
__attribute__((section(".isr_vector"), used))
const void *isr_vector[] = {
    &_estack,
    (void *)&Reset_Handler,       /* -15 Reset */
    (void *)&Default_Handler,     /* -14 NMI */
    (void *)&HardFault_Handler,   /* -13 HardFault */
    (void *)&Default_Handler,     /* -12 MemManage */
    (void *)&Default_Handler,     /* -11 BusFault */
    (void *)&Default_Handler,     /* -10 UsageFault */
    0, 0, 0, 0,                   /*  -9..-6 reserved */
    (void *)&vPortSVCHandler,     /*  -5 SVCall */
    (void *)&Default_Handler,     /*  -4 DebugMonitor */
    0,                            /*  -3 reserved */
    (void *)&xPortPendSVHandler,  /*  -2 PendSV */
    (void *)&xPortSysTickHandler, /*  -1 SysTick */
};

void Reset_Handler(void) {
    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;
    while (dst < &_edata) *dst++ = *src++;

    dst = &_sbss;
    while (dst < &_ebss) *dst++ = 0;

    (void)main();
    for (;;) { } /* main() on this target never returns; trap if it somehow does */
}

static void Default_Handler(void) {
    for (;;) { }
}

static void HardFault_Handler(void) {
    for (;;) { }
}
