/**
  ******************************************************************************
  * @file    radio_e5.h
  * @brief   Niskopoziomowy sterownik modemu radiowego LoRa dla STM32WLE5JC
  *          (Seeed Studio LoRa-E5 mini).
  ******************************************************************************
  */

#ifndef __RADIO_E5_H__
#define __RADIO_E5_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Parametry domyślne sieci LoRa (zgodne z bramą lora_bridge) */
#define RADIO_DEFAULT_FREQ_HZ     868000000UL /* 868.0 MHz (EU868) */
#define RADIO_DEFAULT_SF          7           /* Spreading Factor SF7 */
#define RADIO_DEFAULT_BW          4           /* 125 kHz (kod SX126x: 0x04) */
#define RADIO_DEFAULT_CR          1           /* Coding Rate 4/5 (kod SX126x: 0x01) */
#define RADIO_DEFAULT_SYNC_WORD   0x1424      /* Sieć prywatna LoRa */
#define RADIO_DEFAULT_TX_POWER    14          /* Moc nadawania: +14 dBm (25 mW) */

/**
 * @brief Inicjalizuje modem radiowy LoRa w układzie STM32WLE5JC.
 *        Konfiguruje zasilanie oscylatora TCXO (32 MHz na PB0), przetwornicę DC-DC SMPS,
 *        tor wzmacniacza dużej mocy RFO_HP, przełącznik antenowy RF Switch oraz
 *        parametry modulacji i pakietu LoRa.
 * @return true jeśli konfiguracja powiodła się i self-test rejestrów przeszedł pomyślnie, false w p.p.
 */
bool radio_e5_init(void);

/**
 * @brief Wysyła pakiet danych przez radio LoRa.
 * @param payload Wskaźnik na bufor z danymi
 * @param size Liczba bajtów do wysłania
 * @param timeout_ms Maksymalny czas oczekiwania na zakończenie nadawania (w ms)
 * @return true jeśli pakiet został pomyślnie wysłany w eter (flaga TxDone), false w p.p.
 */
bool radio_e5_send(const uint8_t *payload, uint8_t size, uint32_t timeout_ms);

/**
 * @brief Odbiera pakiet danych przez radio LoRa (okno nasłuchu na ACK).
 * @param buffer Bufor docelowy na odebrany ładunek
 * @param max_size Maksymalny rozmiar bufora
 * @param timeout_ms Czas trwania okna nasłuchu (w ms)
 * @param rssi_out Opcjonalny wskaźnik na zmienną, do której zostanie wpisana wartość RSSI (dBm)
 * @param snr_out Opcjonalny wskaźnik na zmienną, do której zostanie wpisana wartość SNR (dB)
 * @return Liczba odebranych bajtów (>0), 0 jeśli minął timeout, lub ujemna wartość przy błędzie (np. -2 przy błędzie CRC)
 */
int16_t radio_e5_receive(uint8_t *buffer, uint8_t max_size, uint32_t timeout_ms, float *rssi_out, float *snr_out);

/**
 * @brief Przełącza modem w tryb głębokiego uśpienia (Sleep z zachowaniem konfiguracji w RAM).
 */
void radio_e5_sleep(void);

#ifdef __cplusplus
}
#endif

#endif /* __RADIO_E5_H__ */
