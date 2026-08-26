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

/* SoC Kalman filter tuning. Two states are tracked: estimated SoC, and the
   current sensor's own calibration bias (a real ADC/shunt has a small
   constant offset error; left uncorrected, that offset alone causes
   unbounded Coulomb-counting drift no matter how good the rest of the
   model is). Every update predicts SoC by integrating current *after*
   subtracting the current best bias estimate, then fuses in an OCV-lookup
   measurement of SoC directly; the coupling between the two states is what
   lets repeated OCV corrections teach the filter about the bias itself,
   rather than just fighting the same drift every cycle (see
   src/bms/soc_estimator.c). SoC variances are percent^2; bias variance is
   centiamp^2; a state trusted to +/-X (in its own units) has variance X*X. */
#define BMS_SOC_INITIAL_VARIANCE_PCT2      1.0   /* (+/-1%) confidence in the initial 100% assumption */
#define BMS_SOC_CURRENT_SENSE_ERROR_FRAC   0.01  /* fraction of each integrated move left as pure per-sample sensor noise, now that persistent offset is its own state */
#define BMS_SOC_PROCESS_NOISE_FLOOR_PCT2   0.0004 /* (+/-0.02%) added every step so a stale estimate can still be corrected at rest */
#define BMS_SOC_OCV_BASE_VARIANCE_PCT2     0.25  /* (+/-0.5%) OCV measurement noise at true rest */
#define BMS_SOC_OCV_LOAD_COEFF_PCT2_PER_A2 100.0 /* how fast per-amp IR drop de-trusts the OCV reading under load */
#define BMS_SOC_BIAS_INITIAL_VARIANCE_CA2  25.0  /* (+/-5 centiamps, 0.05A) initial uncertainty in current-sensor bias */
#define BMS_SOC_BIAS_RANDOM_WALK_CA2_PER_S 0.0001 /* how fast the bias estimate is allowed to drift over time (calibration offsets change slowly, if at all) */

/* Task periods, ms */
#define TASK_PERIOD_SENSOR_MS   100
#define TASK_PERIOD_SOC_MS      500
#define TASK_PERIOD_FAULT_MS    50
#define TASK_PERIOD_WATCHDOG_MS 200
#define WATCHDOG_STALE_MS       1000  /* a task heartbeat older than this trips the watchdog fault */

#endif /* BMS_CONFIG_H */
