#ifndef ADC_SOIL_H
#define ADC_SOIL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Calibration points measured on this probe (capacitive v1.2, 3.3 V supply).
 * Output voltage drops as moisture rises, so DRY > WET. */
#define SOIL_RAW_DRY  3000U
#define SOIL_RAW_WET  1500U

/* Configures PB3 (ADC_IN2) and calibrates the ADC. Call once before reading. */
void adc_soil_init(void);

/* Averaged 12-bit reading of the soil probe. */
uint16_t adc_soil_read_raw(void);

/* MCU supply voltage in mV, derived from the factory-calibrated VREFINT. */
uint16_t adc_read_vdda_mv(void);

/* Moisture in tenths of a percent (0..1000), clamped to the calibration range. */
uint16_t adc_soil_raw_to_permille(uint16_t raw);

#ifdef __cplusplus
}
#endif

#endif /* ADC_SOIL_H */
