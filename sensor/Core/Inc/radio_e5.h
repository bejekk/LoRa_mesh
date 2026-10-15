#ifndef __RADIO_E5_H__
#define __RADIO_E5_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RADIO_DEFAULT_FREQ_HZ     868000000UL /* 868.0 MHz (EU868) */
#define RADIO_DEFAULT_SF          7           /* Spreading factor SF7 */
#define RADIO_DEFAULT_BW          4           /* 125 kHz (SX126x code: 0x04) */
#define RADIO_DEFAULT_CR          1           /* Coding rate 4/5 (SX126x code: 0x01) */
#define RADIO_DEFAULT_SYNC_WORD   0x1424      /* Private network sync word */
#define RADIO_DEFAULT_TX_POWER    14          /* +14 dBm (25 mW) */

/* Initializes SX126x core, TCXO on PB0, SMPS DC-DC, RFO_HP path, and RF switch. */
bool radio_e5_init(void);

/* Transmits a payload with hardware TxDone verification. */
bool radio_e5_send(const uint8_t *payload, uint8_t size, uint32_t timeout_ms);

/* Opens receive window. Returns received byte count, 0 on timeout, or negative on error. */
int16_t radio_e5_receive(uint8_t *buffer, uint8_t max_size, uint32_t timeout_ms, float *rssi_out, float *snr_out);

/* Puts the sub-GHz radio into warm-retention sleep mode. */
void radio_e5_sleep(void);

#ifdef __cplusplus
}
#endif

#endif /* __RADIO_E5_H__ */
