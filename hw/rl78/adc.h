#ifndef HW_RL78_ADC_H
#define HW_RL78_ADC_H

#include "hw/core/sysbus.h"
#include "hw/core/clock.h"
#include "hw/rl78/intercomm.h"
#include "qemu/timer.h"

enum RL78ADCConvertMode {
    RL78_ADC_CONVERT_MODE_SELECT = 0,
    RL78_ADC_CONVERT_MODE_SCAN,
};
typedef enum RL78ADCConvertMode RL78ADCConvertMode;

enum RL78ADCTriggerMode {
    RL78_ADC_TRIGGER_MODE_SOFTWARE = 0,
    RL78_ADC_TRIGGER_MODE_HARDWARE_NOWAIT,
    RL78_ADC_TRIGGER_MODE_HARDWARE_WAIT,
};
typedef enum RL78ADCTriggerMode RL78ADCTriggerMode;

enum RL78ADCOperationMode {
    RL78_ADC_OPERATION_MODE_CONTINUOUS = 0,
    RL78_ADC_OPERATION_MODE_ONESHOT,
};
typedef enum RL78ADCOperationMode RL78ADCOperationMode;

enum RL78ADCHWTrigger {
    RL78_ADC_HW_TRIGGER_TAU = 0,
    RL78_ADC_HW_TRIGGER_RTC,
    RL78_ADC_HW_TRIGGER_ITL32,
    RL78_ADC_HW_TRIGGER_ELCL,
};
typedef enum RL78ADCHWTrigger RL78ADCHWTrigger;

enum RL78ADCReferenceVoltage {
    RL78_ADC_REFERENCE_VOLTAGE_VDD = 0,
    RL78_ADC_REFERENCE_VOLTAGE_PORT,
    RL78_ADC_REFERENCE_VOLTAGE_INTERNAL,
    RL78_ADC_REFERENCE_VOLTAGE_DISCHARGE,
};
typedef enum RL78ADCReferenceVoltage RL78ADCReferenceVoltage;

enum RL78ADCReferenceGND {
    RL78_ADC_REFERENCE_GND_VDD = 0,
    RL78_ADC_REFERENCE_GND_PORT,
};
typedef enum RL78ADCReferenceGND RL78ADCReferenceGND;

enum RL78ADCResolution {
    RL78_ADC_RESOLUTION_8_BITS = 0,
    RL78_ADC_RESOLUTION_10_BITS,
    RL78_ADC_RESOLUTION_12_BITS,
};
typedef enum RL78ADCResolution RL78ADCResolution;

enum RL78ADCInputSource {
    RL78_ADC_INPUT_SOURCE_ANI0 = 0,
    RL78_ADC_INPUT_SOURCE_ANI1,
    RL78_ADC_INPUT_SOURCE_ANI2,
    RL78_ADC_INPUT_SOURCE_ANI3,
    RL78_ADC_INPUT_SOURCE_ANI4,
    RL78_ADC_INPUT_SOURCE_ANI5,
    RL78_ADC_INPUT_SOURCE_ANI6,
    RL78_ADC_INPUT_SOURCE_ANI7,
    RL78_ADC_INPUT_SOURCE_ANI8,
    RL78_ADC_INPUT_SOURCE_ANI9,
    RL78_ADC_INPUT_SOURCE_ANI10,
    RL78_ADC_INPUT_SOURCE_ANI11,
    RL78_ADC_INPUT_SOURCE_ANI12,
    RL78_ADC_INPUT_SOURCE_ANI13,
    RL78_ADC_INPUT_SOURCE_ANI14,
    RL78_ADC_INPUT_SOURCE_ANI16,
    RL78_ADC_INPUT_SOURCE_ANI17,
    RL78_ADC_INPUT_SOURCE_ANI18,
    RL78_ADC_INPUT_SOURCE_ANI19,
    RL78_ADC_INPUT_SOURCE_ANI20,
    RL78_ADC_INPUT_SOURCE_ANI21,
    RL78_ADC_INPUT_SOURCE_ANI22,
    RL78_ADC_INPUT_SOURCE_ANI23,
    RL78_ADC_INPUT_SOURCE_ANI24,
    RL78_ADC_INPUT_SOURCE_ANI25,
    RL78_ADC_INPUT_SOURCE_ANI26,
    RL78_ADC_INPUT_SOURCE_TSCAP,
    RL78_ADC_INPUT_SOURCE_SENSOR,
    RL78_ADC_INPUT_SOURCE_INTERNAL,
};
typedef enum RL78ADCInputSource RL78ADCInputSource;

enum RL78ADCTestTarget {
    RL78_ADC_TEST_TARGET_NORMAL = 0,
    RL78_ADC_TEST_TARGET_GNDREF,
    RL78_ADC_TEST_TARGET_VDDREF,
};
typedef enum RL78ADCTestTarget RL78ADCTestTarget;

#define RL78_ADC_SOURCE_NUM (27)
struct RL78ADCState {
    /* private */
    SysBusDevice parent_obj;

    /* public */
    Clock *inclk;
    QEMUTimer timer;
    qemu_irq irq;

    MemoryRegion mmio[3];

    bool is_conversion_running;      // ADM0.ADCS bit
    bool is_comparator_enabled;      // ADM0.ADCE bit
    RL78ADCConvertMode convert_mode; // ADM0.ADMD bit
    uint8_t lv;                      // ADM0.LV bits
    uint8_t fr;                      // ADM0.FR bits

    RL78ADCTriggerMode trigger_mode;     // ADM1.ADTMD bits
    RL78ADCOperationMode operation_mode; // ADM1.ADSCM bit
    RL78ADCHWTrigger hw_trigger;         // ADM1.ADTRS bits
    bool is_lowspeed_clock;              // ADM1.ADLSP bit

    RL78ADCReferenceVoltage reference_voltage; // ADM2.ADREFM bit
    RL78ADCReferenceGND reference_gnd;         // ADM2.ADREFP bits
    bool interrupt_in_range;                   // ADM2.ADRCK bit
    bool use_snooze;                           // ADM2.AWC bit
    RL78ADCResolution resolution;              // ADM2.ADTYP bits

    RL78ADCInputSource input_source; // ADS.ADS and ADS.ADISS bits

    uint8_t adul; // ADUL register
    uint8_t adll; // ADLL register

    uint16_t adcr;
    uint16_t scan_adcr[4]; // ADCR0-3 registers for scan mode
    uint8_t scan_index; 

    RL78ADCTestTarget test_target; // ADTES bits

    double adc_results[RL78_ADC_SOURCE_NUM];
};
typedef struct RL78ADCState RL78ADCState;

#define TYPE_RL78_ADC "rl78-adc"
DECLARE_INSTANCE_CHECKER(RL78ADCState, RL78_ADC, TYPE_RL78_ADC)

#endif
