#ifndef SX1262_H
#define SX1262_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Pinout for Seeed XIAO nRF52840 + Wio-SX1262 */
#define SX1262_PIN_NSS      4   /* D4 - Chip Select */
#define SX1262_PIN_RESET    2   /* D2 - Active-low reset */
#define SX1262_PIN_BUSY     3   /* D3 - Busy indicator (HIGH = busy) */
#define SX1262_PIN_DIO1     1   /* D1 - IRQ line (RxDone/TxDone) */
#define SX1262_PIN_RXEN     5   /* D5 - RF switch RX enable */

/* Semtech SX1262 SPI command opcodes */
#define SX1262_CMD_SET_STANDBY              0x80
#define SX1262_CMD_SET_PACKET_TYPE          0x8A
#define SX1262_CMD_GET_PACKET_TYPE          0x11
#define SX1262_CMD_SET_RF_FREQUENCY         0x86
#define SX1262_CMD_SET_PA_CONFIG            0x95
#define SX1262_CMD_SET_TX_PARAMS            0x8E
#define SX1262_CMD_SET_BUFFER_BASE_ADDRESS  0x8F
#define SX1262_CMD_SET_MODULATION_PARAMS    0x8B
#define SX1262_CMD_SET_PACKET_PARAMS        0x8C
#define SX1262_CMD_SET_DIO_IRQ_PARAMS       0x08
#define SX1262_CMD_GET_IRQ_STATUS           0x12
#define SX1262_CMD_CLEAR_IRQ_STATUS         0x02
#define SX1262_CMD_SET_DIO2_AS_RF_SWITCH    0x9D
#define SX1262_CMD_SET_DIO3_AS_TCXO_CTRL    0x97
#define SX1262_CMD_CALIBRATE                0x89
#define SX1262_CMD_SET_REGULATOR_MODE       0x96
#define SX1262_CMD_GET_STATUS               0xC0
#define SX1262_CMD_GET_RX_BUFFER_STATUS     0x13
#define SX1262_CMD_GET_PACKET_STATUS        0x14
#define SX1262_CMD_WRITE_REGISTER           0x0D
#define SX1262_CMD_READ_REGISTER            0x1D
#define SX1262_CMD_WRITE_BUFFER             0x0E
#define SX1262_CMD_READ_BUFFER              0x1E
#define SX1262_CMD_SET_RX                   0x82
#define SX1262_CMD_SET_TX                   0x83

/* IRQ status bitmasks */
#define SX1262_IRQ_TX_DONE                  (1 << 0)
#define SX1262_IRQ_RX_DONE                  (1 << 1)
#define SX1262_IRQ_PREAMBLE_DETECTED        (1 << 2)
#define SX1262_IRQ_SYNC_WORD_VALID          (1 << 3)
#define SX1262_IRQ_HEADER_VALID             (1 << 4)
#define SX1262_IRQ_HEADER_ERR               (1 << 5)
#define SX1262_IRQ_CRC_ERR                  (1 << 6)
#define SX1262_IRQ_CAD_DONE                 (1 << 7)
#define SX1262_IRQ_CAD_DETECTED             (1 << 8)
#define SX1262_IRQ_TIMEOUT                  (1 << 9)
#define SX1262_IRQ_ALL                      (0x03FF)

/* Modulation parameters */
#define SX1262_LORA_SF7                     0x07
#define SX1262_LORA_SF8                     0x08
#define SX1262_LORA_SF9                     0x09
#define SX1262_LORA_SF10                    0x0A
#define SX1262_LORA_SF11                    0x0B
#define SX1262_LORA_SF12                    0x0C

#define SX1262_LORA_BW_125                  0x04
#define SX1262_LORA_BW_250                  0x05
#define SX1262_LORA_BW_500                  0x06

#define SX1262_LORA_CR_4_5                  0x01
#define SX1262_LORA_CR_4_6                  0x02
#define SX1262_LORA_CR_4_7                  0x03
#define SX1262_LORA_CR_4_8                  0x04

typedef struct {
    uint8_t chip_mode;
    uint8_t cmd_status;
    uint8_t raw_byte;
} sx1262_status_t;

void sx1262_init_io(void);
void sx1262_reset(void);
void sx1262_wait_busy(void);

bool sx1262_write_command(uint8_t opcode, const uint8_t *data, uint16_t length);
bool sx1262_read_command(uint8_t opcode, uint8_t *data, uint16_t length);

sx1262_status_t sx1262_get_status(void);

bool sx1262_write_register(uint16_t reg, uint8_t value);
uint8_t sx1262_read_register(uint16_t reg);
bool sx1262_read_register_checked(uint16_t reg, uint8_t *value);

bool sx1262_config_lora(uint32_t freq_hz, uint8_t sf, uint8_t bw, uint8_t cr, uint16_t sync_word);
bool sx1262_start_rx(void);

uint16_t sx1262_get_irq_status(void);
void sx1262_clear_irq_status(uint16_t mask);

bool sx1262_read_buffer(uint8_t offset, uint8_t *data, uint8_t length);
bool sx1262_write_buffer(uint8_t offset, const uint8_t *data, uint8_t length);

bool sx1262_set_tx_power(int8_t power_dbm);
bool sx1262_send_packet(const uint8_t *payload, uint8_t length, uint32_t timeout_ms);
int16_t sx1262_read_packet(uint8_t *payload, uint8_t max_len, float *rssi, float *snr);

#ifdef __cplusplus
}
#endif

#endif /* SX1262_H */
