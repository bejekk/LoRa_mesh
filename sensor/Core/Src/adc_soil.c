#include "adc_soil.h"
#include "main.h"

#define SOIL_CHANNEL     ADC_CHSELR_CHSEL2   /* PB3. PA3 has no ADC input on the STM32WL. */
#define VREFINT_CHANNEL  ADC_CHSELR_CHSEL13

/* Factory VREFINT reading taken at VDDA = 3.3 V (RM0461, "Embedded voltage reference"). */
#define VREFINT_CAL      (*(const volatile uint16_t *)0x1FFF75AAUL)
#define VREFINT_CAL_MV   3300U

#define ADC_CR_RS_BITS   (ADC_CR_ADCAL | ADC_CR_ADSTP | ADC_CR_ADSTART | ADC_CR_ADDIS | ADC_CR_ADEN)
#define WAIT_LIMIT       100000U

/* The "rs" bits of ADC_CR must not be written back with |=, or a stale 1 can
 * re-trigger an operation. Write only the bit being set. */
static void adc_cr_set(uint32_t bit)
{
  ADC->CR = (ADC->CR & ~ADC_CR_RS_BITS) | bit;
}

static void wait_for(volatile uint32_t *reg, uint32_t mask, uint32_t expected)
{
  for (uint32_t n = WAIT_LIMIT; n && ((*reg & mask) != expected); n--) {
  }
}

static uint16_t adc_convert(uint32_t channel)
{
  ADC->ISR = ADC_ISR_CCRDY;
  ADC->CHSELR = channel;
  wait_for(&ADC->ISR, ADC_ISR_CCRDY, ADC_ISR_CCRDY);

  adc_cr_set(ADC_CR_ADSTART);
  wait_for(&ADC->ISR, ADC_ISR_EOC, ADC_ISR_EOC);
  return (uint16_t)ADC->DR; /* reading DR clears EOC */
}

void adc_soil_init(void)
{
  LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOB);
  LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_ADC);

  LL_GPIO_InitTypeDef gpio = {0};
  gpio.Pin = LL_GPIO_PIN_3;
  gpio.Mode = LL_GPIO_MODE_ANALOG;
  gpio.Pull = LL_GPIO_PULL_NO;
  LL_GPIO_Init(GPIOB, &gpio);

  /* The async kernel clock is not configured, so the ADC would have no clock at
   * all and calibration would hang. Use PCLK/4 (12 MHz) instead.
   * Hardware oversampling: 16 samples, >> 4, still a 12-bit result. */
  ADC->CFGR2 = ADC_CFGR2_CKMODE_1
             | ADC_CFGR2_OVSE | ADC_CFGR2_OVSR_0 | ADC_CFGR2_OVSR_1 | ADC_CFGR2_OVSS_2;

  ADC_COMMON->CCR |= ADC_CCR_VREFEN;
  ADC->CR |= ADC_CR_ADVREGEN;
  for (volatile uint32_t i = 0; i < 2000; i++) {
    /* regulator start-up, tADCVREG_STUP = 20 us */
  }

  adc_cr_set(ADC_CR_ADCAL);
  wait_for(&ADC->CR, ADC_CR_ADCAL, 0);

  /* Longest sampling time (160.5 cycles): the probe output is high-impedance and
   * VREFINT needs at least 4 us of sampling. */
  ADC->SMPR = ADC_SMPR_SMP1;

  ADC->ISR = ADC_ISR_ADRDY;
  adc_cr_set(ADC_CR_ADEN);
  wait_for(&ADC->ISR, ADC_ISR_ADRDY, ADC_ISR_ADRDY);
}

uint16_t adc_soil_read_raw(void)
{
  return adc_convert(SOIL_CHANNEL);
}

uint16_t adc_read_vdda_mv(void)
{
  uint16_t vrefint = adc_convert(VREFINT_CHANNEL);
  if (vrefint == 0) {
    return 0;
  }
  return (uint16_t)((VREFINT_CAL_MV * VREFINT_CAL) / vrefint);
}

uint16_t adc_soil_raw_to_permille(uint16_t raw)
{
  if (raw >= SOIL_RAW_DRY) {
    return 0;
  }
  if (raw <= SOIL_RAW_WET) {
    return 1000;
  }
  return (uint16_t)(((uint32_t)(SOIL_RAW_DRY - raw) * 1000U) / (SOIL_RAW_DRY - SOIL_RAW_WET));
}
