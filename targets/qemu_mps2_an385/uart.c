/*
 * Minimal polling driver for the MPS2 CMSDK UART0, used only to get
 * printf() output (via _write() in syscalls.c) onto QEMU's serial console
 * (`-serial stdio`/`-nographic`). Register layout and init values match
 * FreeRTOS's own official demo for this exact QEMU machine
 * (Demo/CORTEX_MPS2_QEMU_IAR_GCC/main.c in the FreeRTOS/FreeRTOS repo).
 */
#include "uart.h"
#include <stdint.h>

#define UART0_BASE 0x40004000UL
#define UART0_DATA    (*(volatile uint32_t *)(UART0_BASE + 0UL))
#define UART0_STATE   (*(volatile uint32_t *)(UART0_BASE + 4UL))
#define UART0_CTRL    (*(volatile uint32_t *)(UART0_BASE + 8UL))
#define UART0_BAUDDIV (*(volatile uint32_t *)(UART0_BASE + 16UL))

#define UART0_STATE_TX_BUFFER_FULL (1UL << 0)
#define UART0_CTRL_TX_ENABLE       (1UL << 0)

void uart_init(void) {
    UART0_BAUDDIV = 16; /* QEMU's UART model doesn't enforce real baud timing; this matches the proven reference value */
    UART0_CTRL = UART0_CTRL_TX_ENABLE;
}

void uart_putc(char c) {
    while (UART0_STATE & UART0_STATE_TX_BUFFER_FULL) { }
    UART0_DATA = (uint32_t)(unsigned char)c;
}
