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
 *
 * CAN-FD, not just classic CAN: dlc here is a plain byte count (0-64), the
 * same convention Linux SocketCAN's own struct canfd_frame.len already
 * uses -- the nonlinear DLC-nibble-to-byte-count table CAN-FD's wire
 * encoding uses is a transport-layer detail can_hal_socketcan.c's real
 * canfd_frame handles, not something this interface or its callers need
 * to know about. Frames of 8 bytes or less behave exactly like classic CAN
 * (see can_protocol.c's BMS_STATUS/FAULT_REPORT/CHARGE_COMMAND, all still
 * classic-sized); CELL_VOLTAGES is the one message that actually needs the
 * larger payload, once BMS_CELL_COUNT grew past 4 cells.
 */

#define CAN_MAX_DLC 64

typedef struct {
    uint32_t id;                 /* 11-bit standard identifier (0-0x7FF) */
    uint8_t dlc;                 /* data length in bytes, 0-64 */
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
