#ifndef CAN_HAL_H
#define CAN_HAL_H

#include <stdint.h>
#include <stddef.h>

/*
 * CAN Hardware Abstraction Layer.
 *
 * Application code (tasks/task_can.c) speaks only this interface. The host
 * build links can_hal_virtual.c: an in-process bus that mimics CAN
 * arbitration (lowest ID wins on contention) and broadcast delivery to every
 * subscriber, with no kernel or hardware dependency.
 *
 * To run on real hardware, implement this header against Linux SocketCAN
 * (socket(PF_CAN, SOCK_RAW, CAN_RAW) on a vcan0/can0 interface) or an MCU's
 * bxCAN/FlexCAN peripheral driver. Application and protocol code do not
 * change. See docs/ARCHITECTURE.md "Portability".
 */

#define CAN_MAX_DLC 8

typedef struct {
    uint32_t id;                 /* 11-bit standard identifier (0-0x7FF) */
    uint8_t dlc;                 /* data length, 0-8 */
    uint8_t data[CAN_MAX_DLC];
} can_frame_t;

typedef void (*can_rx_callback_t)(const can_frame_t *frame, void *ctx);

int can_hal_init(void);
int can_hal_send(const can_frame_t *frame);

/* Register to receive every frame the bus carries; the virtual bus applies
   no hardware filtering, matching how a naive CAN_RAW socket behaves. */
int can_hal_subscribe(can_rx_callback_t cb, void *ctx);
void can_hal_shutdown(void);

#endif /* CAN_HAL_H */
