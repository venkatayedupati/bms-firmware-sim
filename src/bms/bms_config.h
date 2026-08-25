#ifndef BMS_CONFIG_H
#define BMS_CONFIG_H

/* Pack configuration: 4S (four cells in series), simulated. */
#define BMS_CELL_COUNT 4

#define BMS_CELL_NOMINAL_MV      3700
#define BMS_CELL_OVERVOLTAGE_MV  4200
#define BMS_CELL_UNDERVOLTAGE_MV 3000
#define BMS_CELL_IMBALANCE_MV    150   /* max-min spread that trips a fault */

#define BMS_OVERTEMP_C           60
#define BMS_WARNING_MARGIN_MV    50    /* enter WARNING this many mV before the hard limit */

#define BMS_PACK_CAPACITY_MAH    5000  /* nominal pack capacity, milliamp-hours */

/* OCV-correction gating: pack current must stay within +/-this band, for at
   least this long, before resting cell voltage is trusted to correct
   Coulomb-counting drift (loaded voltage includes IR drop and isn't a valid
   OCV sample). */
#define BMS_OCV_REST_CURRENT_CA  5      /* 0.05A */
#define BMS_OCV_REST_MS          30000  /* 30s continuous rest */

/* Task periods, ms */
#define TASK_PERIOD_SENSOR_MS   100
#define TASK_PERIOD_SOC_MS      500
#define TASK_PERIOD_FAULT_MS    50
#define TASK_PERIOD_WATCHDOG_MS 200
#define WATCHDOG_STALE_MS       1000  /* a task heartbeat older than this trips the watchdog fault */

#endif /* BMS_CONFIG_H */
