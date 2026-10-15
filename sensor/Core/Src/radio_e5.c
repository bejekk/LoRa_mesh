#include "radio_e5.h"
#include "main.h"
#include "subghz.h"
#include "cmsis_os2.h"
#include <stdio.h>
#include <string.h>

#define RADIO_IRQ_TX_DONE           (0x0001U)
#define RADIO_IRQ_RX_DONE           (0x0002U)
#define RADIO_IRQ_PREAMBLE_DETECTED (0x0004U)
#define RADIO_IRQ_SYNC_WORD_VALID   (0x0008U)
#define RADIO_IRQ_HEADER_VALID      (0x0010U)
#define RADIO_IRQ_HEADER_ERROR      (0x0020U)
#define RADIO_IRQ_CRC_ERROR         (0x0040U)
#define RADIO_IRQ_TIMEOUT           (0x0200U)
#define RADIO_IRQ_ALL               (0x03FFU)

#define REG_LR_SYNCWORD_MSB         (0x0740U)
#define REG_LR_SYNCWORD_LSB         (0x0741U)

/* LoRa-E5 RF switch: RX (PA4=1, PA5=0), TX RFO_HP (PA4=0, PA5=1) */
static inline void rf_switch_rx(void)
{
  LL_GPIO_SetOutputPin(GPIOA, RF_SW_CTRL1_Pin);
  LL_GPIO_ResetOutputPin(GPIOA, RF_SW_CTRL2_Pin);
}

static inline void rf_switch_tx(void)
{
  LL_GPIO_ResetOutputPin(GPIOA, RF_SW_CTRL1_Pin);
  LL_GPIO_SetOutputPin(GPIOA, RF_SW_CTRL2_Pin);
}

static inline void rf_switch_off(void)
{
  LL_GPIO_ResetOutputPin(GPIOA, RF_SW_CTRL1_Pin);
  LL_GPIO_ResetOutputPin(GPIOA, RF_SW_CTRL2_Pin);
}

static void radio_clear_irq(uint16_t mask)
{
  uint8_t buf[2] = {
    (uint8_t)((mask >> 8) & 0xFF),
    (uint8_t)(mask & 0xFF)
  };
  HAL_SUBGHZ_ExecSetCmd(&hsubghz, RADIO_CLR_IRQSTATUS, buf, 2);
}

static uint16_t radio_get_irq(void)
{
  uint8_t buf[2] = {0};
  HAL_SUBGHZ_ExecGetCmd(&hsubghz, RADIO_GET_IRQSTATUS, buf, 2);
  return ((uint16_t)buf[0] << 8) | buf[1];
}

bool radio_e5_init(void)
{
  uint8_t standby_rc = 0x00;
  if (HAL_SUBGHZ_ExecSetCmd(&hsubghz, RADIO_SET_STANDBY, &standby_rc, 1) != HAL_OK)
  {
    return false;
  }

  uint8_t reg_mode = 0x01; /* SMPS DCDC */
  HAL_SUBGHZ_ExecSetCmd(&hsubghz, RADIO_SET_REGULATORMODE, &reg_mode, 1);

  /* TCXO 32 MHz on PB0: 1.7 V, 5 ms timeout (320 * 15.625 us = 0x000140) */
  uint8_t tcxo_cfg[4] = {0x01, 0x00, 0x01, 0x40};
  HAL_SUBGHZ_ExecSetCmd(&hsubghz, RADIO_SET_TCXOMODE, tcxo_cfg, 4);

  uint8_t standby_xosc = 0x01;
  HAL_SUBGHZ_ExecSetCmd(&hsubghz, RADIO_SET_STANDBY, &standby_xosc, 1);

  uint8_t pkt_type = 0x01; /* PACKET_TYPE_LORA */
  HAL_SUBGHZ_ExecSetCmd(&hsubghz, RADIO_SET_PACKETTYPE, &pkt_type, 1);

  /* Carrier freq: F_RF = (freq * 2^25) / 32 MHz */
  uint32_t freq_raw = (uint32_t)((RADIO_DEFAULT_FREQ_HZ * 33554432ULL) / 32000000ULL);
  uint8_t freq_buf[4] = {
    (uint8_t)(freq_raw >> 24),
    (uint8_t)(freq_raw >> 16),
    (uint8_t)(freq_raw >> 8),
    (uint8_t)(freq_raw)
  };
  HAL_SUBGHZ_ExecSetCmd(&hsubghz, RADIO_SET_RFFREQUENCY, freq_buf, 4);

  /* PA config for LoRa-E5 RFO_HP path (+14 dBm) */
  uint8_t pa_cfg[4] = {0x02, 0x02, 0x00, 0x01};
  HAL_SUBGHZ_ExecSetCmd(&hsubghz, RADIO_SET_PACONFIG, pa_cfg, 4);

  uint8_t tx_params[2] = {RADIO_DEFAULT_TX_POWER, 0x02}; /* ramp 40 us */
  HAL_SUBGHZ_ExecSetCmd(&hsubghz, RADIO_SET_TXPARAMS, tx_params, 2);

  uint8_t base_addr[2] = {0x00, 0x00};
  HAL_SUBGHZ_ExecSetCmd(&hsubghz, RADIO_SET_BUFFERBASEADDRESS, base_addr, 2);

  uint8_t mod_params[4] = {RADIO_DEFAULT_SF, RADIO_DEFAULT_BW, RADIO_DEFAULT_CR, 0x00};
  HAL_SUBGHZ_ExecSetCmd(&hsubghz, RADIO_SET_MODULATIONPARAMS, mod_params, 4);

  /* Preamble: 8, Explicit header, 64-byte payload default, CRC on, standard IQ */
  uint8_t pkt_params[6] = {0x00, 0x08, 0x00, 64, 0x01, 0x00};
  HAL_SUBGHZ_ExecSetCmd(&hsubghz, RADIO_SET_PACKETPARAMS, pkt_params, 6);

  uint8_t dio_cfg[8] = {0x03, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  HAL_SUBGHZ_ExecSetCmd(&hsubghz, RADIO_CFG_DIOIRQ, dio_cfg, 8);

  HAL_SUBGHZ_WriteRegister(&hsubghz, REG_LR_SYNCWORD_MSB, (uint8_t)((RADIO_DEFAULT_SYNC_WORD >> 8) & 0xFF));
  HAL_SUBGHZ_WriteRegister(&hsubghz, REG_LR_SYNCWORD_LSB, (uint8_t)(RADIO_DEFAULT_SYNC_WORD & 0xFF));

  /* Bus verification: read back sync word */
  uint8_t sw_msb = 0, sw_lsb = 0;
  HAL_SUBGHZ_ReadRegister(&hsubghz, REG_LR_SYNCWORD_MSB, &sw_msb);
  HAL_SUBGHZ_ReadRegister(&hsubghz, REG_LR_SYNCWORD_LSB, &sw_lsb);
  uint16_t read_sw = ((uint16_t)sw_msb << 8) | sw_lsb;

  if (read_sw != RADIO_DEFAULT_SYNC_WORD)
  {
    return false;
  }

  radio_clear_irq(RADIO_IRQ_ALL);
  rf_switch_off();
  return true;
}

bool radio_e5_send(const uint8_t *payload, uint8_t size, uint32_t timeout_ms)
{
  if (payload == NULL || size == 0)
  {
    return false;
  }

  rf_switch_tx();
  HAL_SUBGHZ_WriteBuffer(&hsubghz, 0x00, (uint8_t *)payload, size);

  uint8_t pkt_params[6] = {0x00, 0x08, 0x00, size, 0x01, 0x00};
  HAL_SUBGHZ_ExecSetCmd(&hsubghz, RADIO_SET_PACKETPARAMS, pkt_params, 6);
  radio_clear_irq(RADIO_IRQ_ALL);

  uint32_t to_ticks = timeout_ms * 64; /* 1 tick = 15.625 us */
  uint8_t tx_to[3] = {
    (uint8_t)(to_ticks >> 16),
    (uint8_t)(to_ticks >> 8),
    (uint8_t)to_ticks
  };
  HAL_SUBGHZ_ExecSetCmd(&hsubghz, RADIO_SET_TX, tx_to, 3);

  uint32_t start_tick = osKernelGetTickCount();
  bool tx_success = false;

  while ((osKernelGetTickCount() - start_tick) < timeout_ms)
  {
    uint16_t irq = radio_get_irq();
    if (irq & RADIO_IRQ_TX_DONE)
    {
      tx_success = true;
      break;
    }
    if (irq & RADIO_IRQ_TIMEOUT)
    {
      break;
    }
    osDelay(2);
  }

  radio_clear_irq(RADIO_IRQ_ALL);
  rf_switch_off();

  uint8_t standby_rc = 0x00;
  HAL_SUBGHZ_ExecSetCmd(&hsubghz, RADIO_SET_STANDBY, &standby_rc, 1);
  return tx_success;
}

int16_t radio_e5_receive(uint8_t *buffer, uint8_t max_size, uint32_t timeout_ms, float *rssi_out, float *snr_out)
{
  if (buffer == NULL || max_size == 0)
  {
    return -1;
  }

  rf_switch_rx();

  uint8_t pkt_params[6] = {0x00, 0x08, 0x00, max_size, 0x01, 0x00};
  HAL_SUBGHZ_ExecSetCmd(&hsubghz, RADIO_SET_PACKETPARAMS, pkt_params, 6);
  radio_clear_irq(RADIO_IRQ_ALL);

  uint32_t to_ticks = timeout_ms * 64;
  uint8_t rx_to[3] = {
    (uint8_t)(to_ticks >> 16),
    (uint8_t)(to_ticks >> 8),
    (uint8_t)to_ticks
  };
  HAL_SUBGHZ_ExecSetCmd(&hsubghz, RADIO_SET_RX, rx_to, 3);

  uint32_t start_tick = osKernelGetTickCount();
  int16_t result_len = 0;

  while ((osKernelGetTickCount() - start_tick) < (timeout_ms + 50))
  {
    uint16_t irq = radio_get_irq();
    if (irq & RADIO_IRQ_RX_DONE)
    {
      if (irq & RADIO_IRQ_CRC_ERROR)
      {
        result_len = -2;
        break;
      }

      uint8_t rx_buf_status[2] = {0};
      HAL_SUBGHZ_ExecGetCmd(&hsubghz, RADIO_GET_RXBUFFERSTATUS, rx_buf_status, 2);
      uint8_t pkt_len = rx_buf_status[0];
      uint8_t pkt_offset = rx_buf_status[1];

      uint8_t read_bytes = (pkt_len > max_size) ? max_size : pkt_len;
      HAL_SUBGHZ_ReadBuffer(&hsubghz, pkt_offset, buffer, read_bytes);
      result_len = read_bytes;

      if (rssi_out != NULL || snr_out != NULL)
      {
        uint8_t pkt_status[3] = {0};
        HAL_SUBGHZ_ExecGetCmd(&hsubghz, RADIO_GET_PACKETSTATUS, pkt_status, 3);
        if (rssi_out != NULL)
        {
          *rssi_out = -((float)pkt_status[0]) / 2.0f;
        }
        if (snr_out != NULL)
        {
          *snr_out = ((float)((int8_t)pkt_status[1])) / 4.0f;
        }
      }
      break;
    }

    if (irq & RADIO_IRQ_TIMEOUT)
    {
      result_len = 0;
      break;
    }

    osDelay(5);
  }

  radio_clear_irq(RADIO_IRQ_ALL);
  rf_switch_off();

  uint8_t standby_rc = 0x00;
  HAL_SUBGHZ_ExecSetCmd(&hsubghz, RADIO_SET_STANDBY, &standby_rc, 1);
  return result_len;
}

void radio_e5_sleep(void)
{
  rf_switch_off();
  uint8_t sleep_cfg = 0x04; /* Warm retention */
  HAL_SUBGHZ_ExecSetCmd(&hsubghz, RADIO_SET_SLEEP, &sleep_cfg, 1);
}
