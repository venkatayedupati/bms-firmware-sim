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

/* SoC Kalman filter tuning. The filter fuses a Coulomb-counting prediction
   with an OCV-lookup measurement every update; these constants shape how
   much each is trusted rather than gating OCV on/off at a fixed rest
   threshold (see src/bms/soc_estimator.c). All units are percent^2 (a
   variance), so a filter state trusted to +/-X% has a variance of X*X. */
#define BMS_SOC_INITIAL_VARIANCE_PCT2    1.0   /* (+/-1%) confidence in the initial 100% assumption */
#define BMS_SOC_CURRENT_SENSE_ERROR_FRAC 0.05  /* fraction of each integrated move assumed uncertain */
#define BMS_SOC_PROCESS_NOISE_FLOOR_PCT2 0.0004 /* (+/-0.02%) added every step so a stale estimate can still be corrected at rest */
#define BMS_SOC_OCV_BASE_VARIANCE_PCT2   0.25  /* (+/-0.5%) OCV measurement noise at true rest */
#define BMS_SOC_OCV_LOAD_COEFF_PCT2_PER_A2 100.0 /* how fast per-amp IR drop de-trusts the OCV reading under load */

/* Task periods, ms */
#define TASK_PERIOD_SENSOR_MS   100
#define TASK_PERIOD_SOC_MS      500
#define TASK_PERIOD_FAULT_MS    50
#define TASK_PERIOD_WATCHDOG_MS 200
#define WATCHDOG_STALE_MS       1000  /* a task heartbeat older than this trips the watchdog fault */

#endif /* BMS_CONFIG_H */
