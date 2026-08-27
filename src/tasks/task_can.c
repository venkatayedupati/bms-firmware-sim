#include "task_can.h"
#include "../bms/bms_config.h"
#include "../can/can_hal.h"
#include "../util/logger.h"

#define TASK_PERIOD_CAN_MS 100

static void on_charge_command(const can_frame_t *frame, void *ctx_v) {
    (void)ctx_v;
    charge_command_t cmd;
    if (can_protocol_unpack_charge_command(frame, &cmd) == 0) {
        LOGI("can", "RX ChargeCommand: %u.%uA requested", cmd.requested_current_x10 / 10,
             cmd.requested_current_x10 % 10);
    }
}

void task_can_main(void *arg) {
    app_context_t *ctx = (app_context_t *)arg;
    can_hal_subscribe(on_charge_command, ctx);
    LOGI("can", "task started (period=%dms)", TASK_PERIOD_CAN_MS);

    uint16_t last_faults = 0;

    while (!osal_is_shutdown_requested()) {
        osal_mutex_lock(ctx->lock);
        bms_status_t status = ctx->status;
        cell_reading_t reading = ctx->latest_reading;
        uint16_t faults = ctx->fault_mgr.active_faults;
        bms_asil_t severity = ctx->fault_mgr.severity;
        osal_mutex_unlock(ctx->lock);

        can_frame_t frame;

        can_protocol_pack_status(&status, &frame);
        can_hal_send(&frame);

        /* cv.cell_mv and reading.cell_mv are both BMS_CELL_COUNT-sized now
           (cell_voltages_t used to be a fixed 4 slots, independent of
           BMS_CELL_COUNT -- see git history), so this is a plain copy, no
           bounds mismatch to guard against. */
        cell_voltages_t cv;
        for (int i = 0; i < BMS_CELL_COUNT; i++) cv.cell_mv[i] = reading.cell_mv[i];
        can_protocol_pack_cell_voltages(&cv, &frame);
        can_hal_send(&frame);

        if (faults != last_faults) {
            fault_report_t fr = { .fault_bitmask = faults, .severity = (uint8_t)severity };
            can_protocol_pack_fault(&fr, &frame);
            can_hal_send(&frame);
            last_faults = faults;
        }

        osal_task_delay_ms(TASK_PERIOD_CAN_MS);
    }
    LOGI("can", "task exiting");
}
