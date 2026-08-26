/*
 * Linux SocketCAN backend for the CAN HAL -- Linux-only (linux/can.h
 * doesn't exist on macOS), not part of the default build. Binds a
 * PF_CAN/SOCK_RAW/CAN_RAW socket to a real kernel CAN interface (vcan0 by
 * default; override with the BMS_CAN_IFACE environment variable) instead
 * of the in-process virtual bus in can_hal_virtual.c. See
 * docs/ARCHITECTURE.md "Portability" for what running against a real
 * kernel networking stack proves that the virtual bus can't.
 *
 * A CAN_RAW socket only supports one blocking reader; can_hal_subscribe's
 * multi-subscriber fan-out (matching can_hal_virtual.c's contract) is done
 * here in application code, via one background reader thread dispatching
 * to every registered callback -- exactly what the virtual bus's
 * broadcast-to-all-subscribers model already does, just fed from a real
 * socket instead of an in-process call.
 */
/* Same root cause as osal_posix.c's _POSIX_C_SOURCE: glibc hides struct
   ifreq/IFNAMSIZ (net/if.h) under strict -std=c11 without a feature-test
   macro, and this must be defined before any system header is included.
   _DEFAULT_SOURCE is, like _POSIX_C_SOURCE, a real feature-test macro user
   code is specifically meant to define -- not a reserved-identifier
   violation, hence the NOLINT. */
#define _DEFAULT_SOURCE // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

#include "can_hal.h"
#include "../osal/osal.h"

#include <linux/can.h>
#include <linux/can/raw.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <net/if.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <stdatomic.h>

#define MAX_SUBSCRIBERS 8
#define DEFAULT_CAN_IFACE "vcan0"
/* read() on a CAN_RAW socket blocks indefinitely with no built-in way to
   interrupt it; SO_RCVTIMEO gives the reader thread a chance to notice
   can_hal_shutdown() instead of blocking forever past it. */
#define RX_POLL_TIMEOUT_MS 200L /* L: tv_usec is a long; keep the *1000 below widened before it multiplies */

typedef struct {
    can_rx_callback_t cb;
    void *ctx;
} subscriber_t;

static int g_sock = -1;
static osal_mutex_t *g_sub_lock = NULL;
static subscriber_t g_subs[MAX_SUBSCRIBERS];
static int g_sub_count = 0;
static osal_task_t *g_rx_task = NULL;
static atomic_int g_running = 0;

static void rx_task_main(void *arg) {
    (void)arg;
    struct can_frame raw;

    while (atomic_load(&g_running)) {
        ssize_t n = read(g_sock, &raw, sizeof(raw));
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                continue; /* SO_RCVTIMEO expiry or a signal; re-check g_running */
            }
            break; /* socket closed out from under us, or a real error */
        }
        if (n != (ssize_t)sizeof(raw)) continue; /* short/garbled read; drop it */

        can_frame_t frame;
        frame.id = raw.can_id & CAN_SFF_MASK; /* this project only uses 11-bit standard IDs */
        frame.dlc = (raw.can_dlc > CAN_MAX_DLC) ? CAN_MAX_DLC : raw.can_dlc;
        memcpy(frame.data, raw.data, frame.dlc);

        osal_mutex_lock(g_sub_lock);
        subscriber_t local_subs[MAX_SUBSCRIBERS];
        int count = g_sub_count;
        memcpy(local_subs, g_subs, sizeof(subscriber_t) * count);
        osal_mutex_unlock(g_sub_lock);

        for (int i = 0; i < count; i++) {
            if (local_subs[i].cb) local_subs[i].cb(&frame, local_subs[i].ctx);
        }
    }
}

int can_hal_init(void) {
    g_sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (g_sock < 0) return -1;

    const char *iface = getenv("BMS_CAN_IFACE");
    if (!iface) iface = DEFAULT_CAN_IFACE;

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
    if (ioctl(g_sock, SIOCGIFINDEX, &ifr) < 0) {
        close(g_sock);
        g_sock = -1;
        return -1;
    }

    struct sockaddr_can addr;
    memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(g_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(g_sock);
        g_sock = -1;
        return -1;
    }

    struct timeval tv = { .tv_sec = 0, .tv_usec = RX_POLL_TIMEOUT_MS * 1000 };
    setsockopt(g_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    g_sub_lock = osal_mutex_create();
    g_sub_count = 0;
    memset(g_subs, 0, sizeof(g_subs));

    atomic_store(&g_running, 1);
    g_rx_task = osal_task_create("can_socketcan_rx", rx_task_main, NULL, 3);

    return 0;
}

int can_hal_subscribe(can_rx_callback_t cb, void *ctx) {
    if (g_sub_count >= MAX_SUBSCRIBERS) return -1;
    osal_mutex_lock(g_sub_lock);
    g_subs[g_sub_count].cb = cb;
    g_subs[g_sub_count].ctx = ctx;
    g_sub_count++;
    osal_mutex_unlock(g_sub_lock);
    return 0;
}

int can_hal_send(const can_frame_t *frame) {
    if (!frame || frame->dlc > CAN_MAX_DLC || g_sock < 0) return -1;

    struct can_frame raw;
    memset(&raw, 0, sizeof(raw));
    raw.can_id = frame->id & CAN_SFF_MASK; /* standard frame: no EFF/RTR/ERR flag bits */
    raw.can_dlc = frame->dlc;
    memcpy(raw.data, frame->data, frame->dlc);

    ssize_t n = write(g_sock, &raw, sizeof(raw));
    return (n == (ssize_t)sizeof(raw)) ? 0 : -1;
}

void can_hal_shutdown(void) {
    /* Order matters: stop the flag, then join (the reader thread's own
       SO_RCVTIMEO wakes it within RX_POLL_TIMEOUT_MS to notice and exit),
       and only then close the socket -- never close a fd another thread
       might still be blocked reading from. */
    atomic_store(&g_running, 0);
    if (g_rx_task) {
        osal_task_join(g_rx_task);
        g_rx_task = NULL;
    }
    if (g_sock >= 0) {
        close(g_sock);
        g_sock = -1;
    }
    osal_mutex_destroy(g_sub_lock);
    g_sub_lock = NULL;
    g_sub_count = 0;
}
