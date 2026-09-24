#include "sx1262.h"
#include <Arduino.h>
#include <SPI.h>

// Ustawienia magistrali SPI: 2 MHz, MSB First, SPI Mode 0 (CPOL=0, CPHA=0)
static SPISettings spi_settings(2000000, MSBFIRST, SPI_MODE0);

#define SX1262_MAX_RETRIES 3

/**
 * Inicjalizacja linii GPIO i magistrali SPI
 */
void sx1262_init_io(void) {
    // 1. Konfiguracja pinu Chip Select (NSS)
    pinMode(SX1262_PIN_NSS, OUTPUT);
    digitalWrite(SX1262_PIN_NSS, HIGH); // Domyślnie magistrala nieaktywna

    // 2. Konfiguracja pinu RESET
    pinMode(SX1262_PIN_RESET, OUTPUT);
    digitalWrite(SX1262_PIN_RESET, HIGH);

    // 3. Konfiguracja wejścia sygnału BUSY
    pinMode(SX1262_PIN_BUSY, INPUT);

    // 4. Konfiguracja wejścia przerwania DIO1
    pinMode(SX1262_PIN_DIO1, INPUT);

    // 5. Start sprzętowej magistrali SPI
    SPI.begin();
}

/**
 * Czekanie na zakończenie operacji przez SX1262 (pin BUSY musi być LOW)
 */
void sx1262_wait_busy(void) {
    uint32_t start_time = millis();
    while (digitalRead(SX1262_PIN_BUSY) == HIGH) {
        if (millis() - start_time > 100) {
            // Timeout - układ nie odpowiada w ciągu 100ms
            Serial.println("[SX1262 OSTRZEZENIE] Timeout oczekiwania na pin BUSY (LOW)!");
            break;
        }
    }
}

/**
 * Sprzętowy reset układu SX1262
 */
void sx1262_reset(void) {
    // 1. Wymuś stan niski na pinie NRESET
    digitalWrite(SX1262_PIN_RESET, LOW);
    delayMicroseconds(200); // Wymagany czas w stanie LOW według datasheetu: min. 100 us

    // 2. Przywróć stan wysoki
    digitalWrite(SX1262_PIN_RESET, HIGH);
    delay(10); // Odczekaj chwilę na uruchomienie wewnętrznego oscylatora RC

    // 3. Poczekaj na gotowość układu
    sx1262_wait_busy();
}

/**
 * Wysłanie komendy bez odczytu danych (OpCode + parametry) z obsługą ponawiania
 */
bool sx1262_write_command(uint8_t opcode, const uint8_t *data, uint16_t length) {
    for (uint8_t attempt = 0; attempt < SX1262_MAX_RETRIES; attempt++) {
        sx1262_wait_busy();

        SPI.beginTransaction(spi_settings);
        digitalWrite(SX1262_PIN_NSS, LOW); // CS = LOW (początek ramki)

        // Wysłanie bajtu rozkazu (OpCode)
        SPI.transfer(opcode);

        // Wysłanie kolejnych bajtów parametrów
        for (uint16_t i = 0; i < length; i++) {
            SPI.transfer(data[i]);
        }

        digitalWrite(SX1262_PIN_NSS, HIGH); // CS = HIGH (koniec ramki)
        SPI.endTransaction();

        // Po komendzie układ wchodzi na chwilę w stan BUSY
        sx1262_wait_busy();

        return true;
    }
    return false;
}

/**
 * Wysłanie komendy z odczytem danych (OpCode + NOP + odczyt) z weryfikacją statusu komendy
 */
bool sx1262_read_command(uint8_t opcode, uint8_t *data, uint16_t length) {
    for (uint8_t attempt = 0; attempt < SX1262_MAX_RETRIES; attempt++) {
        sx1262_wait_busy();

        SPI.beginTransaction(spi_settings);
        digitalWrite(SX1262_PIN_NSS, LOW);

        // 1. Wysłanie OpCode
        SPI.transfer(opcode);

        // 2. Faza NOP (Dummy byte) - odczytujemy bajt statusu z MISO
        uint8_t status_byte = SPI.transfer(0x00);
        uint8_t cmd_status = (status_byte >> 1) & 0x07;

        // 3. Weryfikacja: jeśli układ zgłasza błąd wykonania (0x03, 0x04, 0x05)
        if (cmd_status >= 0x03 && cmd_status <= 0x05) {
            digitalWrite(SX1262_PIN_NSS, HIGH);
            SPI.endTransaction();

            Serial.print("[SX1262 BLAD SPI] Komenda 0x");
            Serial.print(opcode, HEX);
            Serial.print(" zglosila blad statusu: 0x");
            Serial.print(cmd_status, HEX);
            Serial.print(" (proba ");
            Serial.print(attempt + 1);
            Serial.println(")");

            sx1262_wait_busy();
            delayMicroseconds(500);
            continue; // Ponawiamy próbę
        }

        // 4. Odczyt właściwych danych
        for (uint16_t i = 0; i < length; i++) {
            data[i] = SPI.transfer(0x00);
        }

        digitalWrite(SX1262_PIN_NSS, HIGH);
        SPI.endTransaction();

        sx1262_wait_busy();
        return true;
    }

    return false;
}

/**
 * Odczyt statusu układu (Komenda GetStatus: 0xC0)
 */
sx1262_status_t sx1262_get_status(void) {
    sx1262_wait_busy();

    SPI.beginTransaction(spi_settings);
    digitalWrite(SX1262_PIN_NSS, LOW);

    // Wysłanie GetStatus (0xC0)
    uint8_t raw = SPI.transfer(SX1262_CMD_GET_STATUS);
    uint8_t status_byte = SPI.transfer(0x00);

    digitalWrite(SX1262_PIN_NSS, HIGH);
    SPI.endTransaction();

    uint8_t final_byte = (status_byte != 0x00 && status_byte != 0xFF) ? status_byte : raw;

    sx1262_status_t status;
    status.raw_byte = final_byte;
    status.chip_mode = (final_byte >> 4) & 0x07;    // Bity 6..4
    status.cmd_status = (final_byte >> 1) & 0x07;   // Bity 3..1

    return status;
}

/**
 * Zapis rejestru 16-bitowego (OpCode 0x0D + Adres MSB + Adres LSB + Wartość)
 */
bool sx1262_write_register(uint16_t reg, uint8_t value) {
    uint8_t buf[3];
    buf[0] = (uint8_t)((reg >> 8) & 0xFF); // Adres MSB
    buf[1] = (uint8_t)(reg & 0xFF);        // Adres LSB
    buf[2] = value;                        // Wartość do wpisania

    return sx1262_write_command(SX1262_CMD_WRITE_REGISTER, buf, 3);
}

/**
 * Odczyt rejestru 16-bitowego z weryfikacją błędu (OpCode 0x1D + Adres MSB + Adres LSB + NOP + Odczyt)
 */
bool sx1262_read_register_checked(uint16_t reg, uint8_t *value) {
    for (uint8_t attempt = 0; attempt < SX1262_MAX_RETRIES; attempt++) {
        sx1262_wait_busy();

        SPI.beginTransaction(spi_settings);
        digitalWrite(SX1262_PIN_NSS, LOW);

        SPI.transfer(SX1262_CMD_READ_REGISTER);
        SPI.transfer((uint8_t)((reg >> 8) & 0xFF));
        SPI.transfer((uint8_t)(reg & 0xFF));

        // NOP 1 – w trakcie tego bajtu układ zwraca status na MISO
        uint8_t status_byte = SPI.transfer(0x00);
        uint8_t cmd_status = (status_byte >> 1) & 0x07;

        // Weryfikacja błędu
        if (cmd_status >= 0x03 && cmd_status <= 0x05) {
            digitalWrite(SX1262_PIN_NSS, HIGH);
            SPI.endTransaction();

            Serial.print("[SX1262 BLAD SPI] Odczyt rejestru 0x");
            Serial.print(reg, HEX);
            Serial.print(" zglosil blad: 0x");
            Serial.println(cmd_status, HEX);

            sx1262_wait_busy();
            delayMicroseconds(500);
            continue; // Ponawiamy próbę
        }

        // NOP 2 – dopiero teraz na MISO pojawia się właściwa wartość rejestru
        // (potwierdzono diagnostycznie: dane są w b4, nie b3)
        *value = SPI.transfer(0x00);

        digitalWrite(SX1262_PIN_NSS, HIGH);
        SPI.endTransaction();

        sx1262_wait_busy();
        return true;
    }

    return false;
}

/**
 * Uproszczony odczyt rejestru zwracający wartość bezpośrednio
 */
uint8_t sx1262_read_register(uint16_t reg) {
    uint8_t val = 0;
    sx1262_read_register_checked(reg, &val);
    return val;
}
