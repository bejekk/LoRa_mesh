#ifndef SX1262_H
#define SX1262_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * ============================================================================
 * Definicje wyprowadzeń (Pinout) dla Seeed XIAO nRF52840 + Wio-SX1262 Kit
 * ============================================================================
 */
#define SX1262_PIN_NSS      4   // D3 - Chip Select (SPI CS)
#define SX1262_PIN_RESET    2   // D2 - Sprzętowy Reset (aktywny stanem LOW)
#define SX1262_PIN_BUSY     3   // D1 - Sygnał zajętości układu (HIGH = zajęty)
#define SX1262_PIN_DIO1     1   // D0 - Linia przerwań (np. RxDone)


/**
 * ============================================================================
 * Kody rozkazów SPI (OpCodes) według dokumentacji Semtech SX1262
 * ============================================================================
 */
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
#define SX1262_CMD_SET_REGULATOR_MODE       0x96
#define SX1262_CMD_GET_STATUS               0xC0
#define SX1262_CMD_WRITE_REGISTER           0x0D
#define SX1262_CMD_READ_REGISTER            0x1D
#define SX1262_CMD_WRITE_BUFFER             0x0E
#define SX1262_CMD_READ_BUFFER              0x1E
#define SX1262_CMD_SET_RX                   0x82
#define SX1262_CMD_SET_TX                   0x83

/**
 * ============================================================================
 * Kody statusu wykonania komendy (Command Status - bity 3..1 w bajcie statusu)
 * ============================================================================
 */
#define SX1262_CMD_STATUS_RESERVED          0x00
#define SX1262_CMD_STATUS_RFU               0x01
#define SX1262_CMD_STATUS_DATA_AVAILABLE    0x02 // Sukces - dane gotowe / komenda OK
#define SX1262_CMD_STATUS_TIMEOUT           0x03 // Błąd: Timeout
#define SX1262_CMD_STATUS_PROCESSING_ERROR  0x04 // Błąd: Błąd przetwarzania komendy
#define SX1262_CMD_STATUS_EXECUTION_FAILURE 0x05 // Błąd: Niepoprawne parametry / błąd wykonania
#define SX1262_CMD_STATUS_TX_DONE           0x06 // Nadawanie pakietu TX zakończone

/**
 * ============================================================================
 * Struktura statusu układu (zwracana przez GetStatus - 1 bajt)
 * ============================================================================
 * Bity 6:4 -> ChipMode (np. 0x02 = STDBY_RC, 0x03 = STDBY_XOSC, 0x05 = RX, 0x06 = TX)
 * Bity 3:1 -> CommandStatus (np. 0x02 = Data Available, 0x03 = Cmd Timeout, 0x04 = Cmd Error)
 */
typedef struct {
    uint8_t chip_mode;
    uint8_t cmd_status;
    uint8_t raw_byte;
} sx1262_status_t;

/**
 * ============================================================================
 * Prototypy funkcji sterownika
 * ============================================================================
 */

// Inicjalizacja pinów GPIO oraz magistrali SPI
void sx1262_init_io(void);

// Sprzętowy reset układu (puls LOW na NRESET)
void sx1262_reset(void);

// Czekanie aż pin BUSY spadnie do stanu LOW
void sx1262_wait_busy(void);

// Wysłanie komendy bez danych zwrotnych (z obsługą powtórzeń w razie błędu)
bool sx1262_write_command(uint8_t opcode, const uint8_t *data, uint16_t length);

// Wysłanie komendy i odczyt danych zwrotnych (z weryfikacją statusu i powtórzeniami)
bool sx1262_read_command(uint8_t opcode, uint8_t *data, uint16_t length);

// Odczyt statusu układu (GetStatus)
sx1262_status_t sx1262_get_status(void);

// Zapis pojedynczego rejestru 16-bitowego
bool sx1262_write_register(uint16_t reg, uint8_t value);

// Odczyt pojedynczego rejestru 16-bitowego
uint8_t sx1262_read_register(uint16_t reg);
bool sx1262_read_register_checked(uint16_t reg, uint8_t *value);

#ifdef __cplusplus
}
#endif

#endif // SX1262_H
