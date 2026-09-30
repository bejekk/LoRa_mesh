/**
  ******************************************************************************
  * @file    radio_e5.c
  * @brief   Implementacja niskopoziomowego sterownika radiowego dla Seeed LoRa-E5
  *          (STM32WLE5JC).
  ******************************************************************************
  */

#include "radio_e5.h"
#include "main.h"
#include "subghz.h"
#include "cmsis_os2.h"
#include <stdio.h>
#include <string.h>

/* Flagi przerwań modemu radiowego SX126x */
#define RADIO_IRQ_TX_DONE           (0x0001U)
#define RADIO_IRQ_RX_DONE           (0x0002U)
#define RADIO_IRQ_PREAMBLE_DETECTED (0x0004U)
#define RADIO_IRQ_SYNC_WORD_VALID   (0x0008U)
#define RADIO_IRQ_HEADER_VALID      (0x0010U)
#define RADIO_IRQ_HEADER_ERROR      (0x0020U)
#define RADIO_IRQ_CRC_ERROR         (0x0040U)
#define RADIO_IRQ_TIMEOUT           (0x0200U)
#define RADIO_IRQ_ALL               (0x03FFU)

/* Rejestry modemu SX126x dla Sync Word */
#define REG_LR_SYNCWORD_MSB         (0x0740U)
#define REG_LR_SYNCWORD_LSB         (0x0741U)

/**
 * @brief Przełącza zewnętrzny układ przełącznika antenowego (RF Switch) w tryb odbioru (RX)
 *        Tabela prawdy LoRa-E5 mini: PA4 = 1, PA5 = 0
 */
static inline void rf_switch_rx(void)
{
  LL_GPIO_SetOutputPin(GPIOA, RF_SW_CTRL1_Pin);
  LL_GPIO_ResetOutputPin(GPIOA, RF_SW_CTRL2_Pin);
}

/**
 * @brief Przełącza zewnętrzny układ przełącznika antenowego (RF Switch) w tryb nadawania (TX)
 *        Tabela prawdy LoRa-E5 mini: PA4 = 0, PA5 = 1 (tor RFO_HP)
 */
static inline void rf_switch_tx(void)
{
  LL_GPIO_ResetOutputPin(GPIOA, RF_SW_CTRL1_Pin);
  LL_GPIO_SetOutputPin(GPIOA, RF_SW_CTRL2_Pin);
}

/**
 * @brief Wyłącza przełącznik antenowy (stan spoczynku / uśpienie)
 *        Tabela prawdy LoRa-E5 mini: PA4 = 0, PA5 = 0
 */
static inline void rf_switch_off(void)
{
  LL_GPIO_ResetOutputPin(GPIOA, RF_SW_CTRL1_Pin);
  LL_GPIO_ResetOutputPin(GPIOA, RF_SW_CTRL2_Pin);
}

/**
 * @brief Czyści wybrane flagi przerwań modemu radiowego
 */
static void radio_clear_irq(uint16_t mask)
{
  uint8_t buf[2];
  buf[0] = (uint8_t)((mask >> 8) & 0xFF);
  buf[1] = (uint8_t)(mask & 0xFF);
  HAL_SUBGHZ_ExecSetCmd(&hsubghz, RADIO_CLR_IRQSTATUS, buf, 2);
}

/**
 * @brief Odczytuje aktualny stan 16-bitowego rejestru flag przerwań IRQ modemu
 */
static uint16_t radio_get_irq(void)
{
  uint8_t buf[2] = {0};
  HAL_SUBGHZ_ExecGetCmd(&hsubghz, RADIO_GET_IRQSTATUS, buf, 2);
  return ((uint16_t)buf[0] << 8) | buf[1];
}

bool radio_e5_init(void)
{
  printf("[Radio-E5] Inicjalizacja modemu SX126x w STM32WLE5JC...\r\n");

  // 1. Wejście w tryb czuwania z wewnętrznym oscylatorem RC (STDBY_RC)
  uint8_t standby_rc = 0x00;
  if (HAL_SUBGHZ_ExecSetCmd(&hsubghz, RADIO_SET_STANDBY, &standby_rc, 1) != HAL_OK)
  {
    printf("[Radio-E5 BLAD] Nie udalo sie przejsc do STDBY_RC!\r\n");
    return false;
  }

  // 2. Włączenie zasilania wewnętrznej przetwornicy DC-DC (SMPS) dla maksymalnej wydajności prądowej
  uint8_t reg_mode = 0x01; // USE_DCDC
  HAL_SUBGHZ_ExecSetCmd(&hsubghz, RADIO_SET_REGULATORMODE, &reg_mode, 1);

  // 3. Konfiguracja zasilania oscylatora TCXO 32 MHz na pinie PB0
  // Napięcie: 1.7V (0x01), czas wybudzenia: 5ms (320 * 15.625us = 0x000140)
  uint8_t tcxo_cfg[4] = {0x01, 0x00, 0x01, 0x40};
  HAL_SUBGHZ_ExecSetCmd(&hsubghz, RADIO_SET_TCXOMODE, tcxo_cfg, 4);

  // 4. Przejście w tryb czuwania z aktywnym oscylatorem TCXO (STDBY_XOSC)
  uint8_t standby_xosc = 0x01;
  HAL_SUBGHZ_ExecSetCmd(&hsubghz, RADIO_SET_STANDBY, &standby_xosc, 1);

  // 5. Wybór modulacji LoRa
  uint8_t pkt_type = 0x01; // PACKET_TYPE_LORA
  HAL_SUBGHZ_ExecSetCmd(&hsubghz, RADIO_SET_PACKETTYPE, &pkt_type, 1);

  // 6. Ustawienie częstotliwości nośnej (868.0 MHz)
  // Wzór: F_RF = (freq * 2^25) / 32MHz = 0x36400000 dla 868.0 MHz
  uint32_t freq_raw = (uint32_t)((RADIO_DEFAULT_FREQ_HZ * 33554432ULL) / 32000000ULL);
  uint8_t freq_buf[4] = {
    (uint8_t)(freq_raw >> 24),
    (uint8_t)(freq_raw >> 16),
    (uint8_t)(freq_raw >> 8),
    (uint8_t)(freq_raw)
  };
  HAL_SUBGHZ_ExecSetCmd(&hsubghz, RADIO_SET_RFFREQUENCY, freq_buf, 4);

  // 7. Konfiguracja wzmacniacza mocy (Power Amplifier) dla LoRa-E5
  // W LoRa-E5 tor antenowy jest wyprowadzony wyłącznie z pinu dużej mocy RFO_HP.
  // Parametry: paDutyCycle=0x02, hpMax=0x02, deviceSel=0x00 (HP), paLut=0x01 (+14 dBm)
  uint8_t pa_cfg[4] = {0x02, 0x02, 0x00, 0x01};
  HAL_SUBGHZ_ExecSetCmd(&hsubghz, RADIO_SET_PACONFIG, pa_cfg, 4);

  // 8. Ustawienie mocy wyjściowej (+14 dBm) i czasu narastania zbocza (40 us)
  uint8_t tx_params[2] = {RADIO_DEFAULT_TX_POWER, 0x02};
  HAL_SUBGHZ_ExecSetCmd(&hsubghz, RADIO_SET_TXPARAMS, tx_params, 2);

  // 9. Ustawienie adresu bazowego bufora FIFO (TX = 0x00, RX = 0x00)
  uint8_t base_addr[2] = {0x00, 0x00};
  HAL_SUBGHZ_ExecSetCmd(&hsubghz, RADIO_SET_BUFFERBASEADDRESS, base_addr, 2);

  // 10. Parametry modulacji LoRa: SF7, BW 125 kHz (0x04), CR 4/5 (0x01), LDRO off (0x00)
  uint8_t mod_params[4] = {RADIO_DEFAULT_SF, RADIO_DEFAULT_BW, RADIO_DEFAULT_CR, 0x00};
  HAL_SUBGHZ_ExecSetCmd(&hsubghz, RADIO_SET_MODULATIONPARAMS, mod_params, 4);

  // 11. Parametry ramki pakietu:
  // Preambuła: 8 symboli, Nagłówek: Explicit (0x00), Rozmiar ładunku: 64, CRC: włączone (0x01), Standard IQ (0x00)
  uint8_t pkt_params[6] = {0x00, 0x08, 0x00, 64, 0x01, 0x00};
  HAL_SUBGHZ_ExecSetCmd(&hsubghz, RADIO_SET_PACKETPARAMS, pkt_params, 6);

  // 12. Przekierowanie zdarzeń do rejestru IRQ (wszystkie flagi aktywne)
  uint8_t dio_cfg[8] = {0x03, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  HAL_SUBGHZ_ExecSetCmd(&hsubghz, RADIO_CFG_DIOIRQ, dio_cfg, 8);

  // 13. Zapis słowa synchronizacji Sync Word (0x1424 - sieć prywatna LoRa)
  HAL_SUBGHZ_WriteRegister(&hsubghz, REG_LR_SYNCWORD_MSB, (uint8_t)((RADIO_DEFAULT_SYNC_WORD >> 8) & 0xFF));
  HAL_SUBGHZ_WriteRegister(&hsubghz, REG_LR_SYNCWORD_LSB, (uint8_t)(RADIO_DEFAULT_SYNC_WORD & 0xFF));

  // 14. Sprzętowy Self-Test: Odczyt powrotny rejestrów Sync Word przez magistralę SUBGHZSPI
  uint8_t sw_msb = 0, sw_lsb = 0;
  HAL_SUBGHZ_ReadRegister(&hsubghz, REG_LR_SYNCWORD_MSB, &sw_msb);
  HAL_SUBGHZ_ReadRegister(&hsubghz, REG_LR_SYNCWORD_LSB, &sw_lsb);
  uint16_t read_sync_word = ((uint16_t)sw_msb << 8) | sw_lsb;

  if (read_sync_word != RADIO_DEFAULT_SYNC_WORD)
  {
    printf("[Radio-E5 BLAD] Self-test magistrali SPI nie powiodl sie! Odczytano SyncWord: 0x%04X (oczekiwano 0x%04X)\r\n",
           read_sync_word, RADIO_DEFAULT_SYNC_WORD);
    return false;
  }

  // 15. Wyczyszczenie wszystkich flag i wyłączenie przełącznika RF
  radio_clear_irq(RADIO_IRQ_ALL);
  rf_switch_off();

  printf("[Radio-E5 OK] Modem LoRa 868.0 MHz (SF%u, BW125, CR4/5, SyncWord 0x%04X) gotowy!\r\n",
         RADIO_DEFAULT_SF, read_sync_word);
  return true;
}

bool radio_e5_send(const uint8_t *payload, uint8_t size, uint32_t timeout_ms)
{
  if (payload == NULL || size == 0)
  {
    return false;
  }

  // 1. Przełącz przełącznik antenowy w tryb nadawania (PA4=0, PA5=1)
  rf_switch_tx();

  // 2. Załaduj ładunek do bufora FIFO modemu od pozycji 0x00
  HAL_SUBGHZ_WriteBuffer(&hsubghz, 0x00, (uint8_t*)payload, size);

  // 3. Ustaw parametry ramki z rzeczywistą długością ładunku
  uint8_t pkt_params[6] = {0x00, 0x08, 0x00, size, 0x01, 0x00};
  HAL_SUBGHZ_ExecSetCmd(&hsubghz, RADIO_SET_PACKETPARAMS, pkt_params, 6);

  // 4. Wyczyść flagi przerwań przed startem nadawania
  radio_clear_irq(RADIO_IRQ_ALL);

  // 5. Uruchom nadawanie (SetTx) z timeoutem
  // 1 jednostka timeoutu = 15.625 us (64 jednostki = 1 ms)
  uint32_t to_ticks = timeout_ms * 64;
  uint8_t tx_to[3] = {
    (uint8_t)(to_ticks >> 16),
    (uint8_t)(to_ticks >> 8),
    (uint8_t)to_ticks
  };
  HAL_SUBGHZ_ExecSetCmd(&hsubghz, RADIO_SET_TX, tx_to, 3);

  // 6. Oczekiwanie na zakończenie transmisji (flaga TxDone w rejestrze IRQ)
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
      printf("[Radio-E5] Timeout sprzetowy nadajnika TX!\r\n");
      break;
    }

    osDelay(2); // Odpoczynek wątku na 2 ms (FreeRTOS)
  }

  // 7. Sprzątanie: wyczyść flagi, wyłącz przełącznik RF i przejdź w Standby
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

  // 1. Przełącz przełącznik antenowy w tryb odbioru (PA4=1, PA5=0)
  rf_switch_rx();

  // 2. Ustaw maksymalny oczekiwany rozmiar bufora
  uint8_t pkt_params[6] = {0x00, 0x08, 0x00, max_size, 0x01, 0x00};
  HAL_SUBGHZ_ExecSetCmd(&hsubghz, RADIO_SET_PACKETPARAMS, pkt_params, 6);

  // 3. Wyczyść flagi przerwań
  radio_clear_irq(RADIO_IRQ_ALL);

  // 4. Uruchom odbiornik (SetRx) z timeoutem w tickach (1 ms = 64 ticki)
  uint32_t to_ticks = timeout_ms * 64;
  uint8_t rx_to[3] = {
    (uint8_t)(to_ticks >> 16),
    (uint8_t)(to_ticks >> 8),
    (uint8_t)to_ticks
  };
  HAL_SUBGHZ_ExecSetCmd(&hsubghz, RADIO_SET_RX, rx_to, 3);

  // 5. Oczekiwanie na odebranie ramki w oknie czasowym
  uint32_t start_tick = osKernelGetTickCount();
  int16_t result_len = 0;

  while ((osKernelGetTickCount() - start_tick) < (timeout_ms + 50))
  {
    uint16_t irq = radio_get_irq();

    if (irq & RADIO_IRQ_RX_DONE)
    {
      // Weryfikacja sumy kontrolnej CRC
      if (irq & RADIO_IRQ_CRC_ERROR)
      {
        printf("[Radio-E5] Odebrano ramke z bledem sumy CRC!\r\n");
        result_len = -2;
        break;
      }

      // Odczyt statusu bufora RX (długość odebranego pakietu i offset początkowy)
      uint8_t rx_buf_status[2] = {0};
      HAL_SUBGHZ_ExecGetCmd(&hsubghz, RADIO_GET_RXBUFFERSTATUS, rx_buf_status, 2);
      uint8_t pkt_len = rx_buf_status[0];
      uint8_t pkt_offset = rx_buf_status[1];

      // Kopiowanie danych z bufora FIFO modemu
      uint8_t read_bytes = (pkt_len > max_size) ? max_size : pkt_len;
      HAL_SUBGHZ_ReadBuffer(&hsubghz, pkt_offset, buffer, read_bytes);
      result_len = read_bytes;

      // Odczyt jakości sygnału (RSSI i SNR)
      if (rssi_out != NULL || snr_out != NULL)
      {
        uint8_t pkt_status[3] = {0}; // [0]: rssiPkt, [1]: snrPkt, [2]: signalRssiPkt
        HAL_SUBGHZ_ExecGetCmd(&hsubghz, RADIO_GET_PACKETSTATUS, pkt_status, 3);

        if (rssi_out != NULL)
        {
          *rssi_out = -((float)pkt_status[0]) / 2.0f;
        }
        if (snr_out != NULL)
        {
          int8_t raw_snr = (int8_t)pkt_status[1];
          *snr_out = ((float)raw_snr) / 4.0f;
        }
      }
      break;
    }

    if (irq & RADIO_IRQ_TIMEOUT)
    {
      // Minął czas okna odbiorczego
      result_len = 0;
      break;
    }

    osDelay(5); // Odpoczynek wątku na 5 ms (FreeRTOS)
  }

  // 6. Sprzątanie: wyczyść flagi, wyłącz przełącznik RF i powróć do Standby
  radio_clear_irq(RADIO_IRQ_ALL);
  rf_switch_off();

  uint8_t standby_rc = 0x00;
  HAL_SUBGHZ_ExecSetCmd(&hsubghz, RADIO_SET_STANDBY, &standby_rc, 1);

  return result_len;
}

void radio_e5_sleep(void)
{
  rf_switch_off();

  // Tryb uśpienia z zachowaniem konfiguracji w wewnętrznym buforze RAM (Warm Start = 0x04)
  uint8_t sleep_cfg = 0x04;
  HAL_SUBGHZ_ExecSetCmd(&hsubghz, RADIO_SET_SLEEP, &sleep_cfg, 1);
}
