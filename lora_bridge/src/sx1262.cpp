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

    // 5. Konfiguracja pinu przełącznika antenowego RXEN (D5)
    pinMode(SX1262_PIN_RXEN, OUTPUT);
    digitalWrite(SX1262_PIN_RXEN, LOW);

    // 6. Start sprzętowej magistrali SPI
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

        // 3. Weryfikacja: jeśli układ zgłasza błąd przetwarzania (0x04) lub timeout (0x03)
        if (cmd_status == 0x03 || cmd_status == 0x04) {
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
        if (cmd_status == 0x03 || cmd_status == 0x04) {
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

/**
 * Bezpośredni odczyt bufora FIFO SX1262 (OpCode 0x1E)
 */
bool sx1262_read_buffer(uint8_t offset, uint8_t *data, uint8_t length) {
    sx1262_wait_busy();

    SPI.beginTransaction(spi_settings);
    digitalWrite(SX1262_PIN_NSS, LOW);

    SPI.transfer(SX1262_CMD_READ_BUFFER); // 0x1E
    SPI.transfer(offset);
    SPI.transfer(0x00); // NOP dummy byte (status byte)

    for (uint8_t i = 0; i < length; i++) {
        data[i] = SPI.transfer(0x00);
    }

    digitalWrite(SX1262_PIN_NSS, HIGH);
    SPI.endTransaction();

    sx1262_wait_busy();
    return true;
}

/**
 * Odczyt 16-bitowego rejestru flag przerwań IRQ
 */
uint16_t sx1262_get_irq_status(void) {
    uint8_t buf[2] = {0};
    sx1262_read_command(SX1262_CMD_GET_IRQ_STATUS, buf, 2);
    return ((uint16_t)buf[0] << 8) | buf[1];
}

/**
 * Czyszczenie flag przerwań IRQ
 */
void sx1262_clear_irq_status(uint16_t mask) {
    uint8_t buf[2];
    buf[0] = (uint8_t)((mask >> 8) & 0xFF);
    buf[1] = (uint8_t)(mask & 0xFF);
    sx1262_write_command(SX1262_CMD_CLEAR_IRQ_STATUS, buf, 2);
}

/**
 * Pełna konfiguracja parametrów modemu LoRa
 */
bool sx1262_config_lora(uint32_t freq_hz, uint8_t sf, uint8_t bw, uint8_t cr, uint16_t sync_word) {
    // 1. Wejście w tryb czuwania (STDBY_RC)
    uint8_t standby = 0x00;
    sx1262_write_command(SX1262_CMD_SET_STANDBY, &standby, 1);

    // 2. Włączenie regulatora przetwornicy DC-DC (optymalna praca dla Wio-SX1262)
    uint8_t reg_mode = 0x01; // USE_DCDC
    sx1262_write_command(SX1262_CMD_SET_REGULATOR_MODE, &reg_mode, 1);

    // 2b. Włączenie zasilania aktywnego oscylatora TCXO przez linię DIO3 (Kluczowe dla Wio-SX1262!)
    // Napięcie 1.7V (0x01), opóźnienie wybudzenia 5ms (320 * 15.625us = 0x000140)
    uint8_t tcxo_buf[4] = {0x01, 0x00, 0x01, 0x40};
    sx1262_write_command(SX1262_CMD_SET_DIO3_AS_TCXO_CTRL, tcxo_buf, 4);

    // 2c. Uruchomienie oscylatora TCXO poprzez przejście w STDBY_XOSC
    uint8_t stdby_xosc = 0x01; // STDBY_XOSC
    sx1262_write_command(SX1262_CMD_SET_STANDBY, &stdby_xosc, 1);
    delay(10);

    // 3. Automatyczne sterowanie wewnętrznym przełącznikiem RF przez linię DIO2
    uint8_t dio2_rf_switch = 0x01;
    sx1262_write_command(SX1262_CMD_SET_DIO2_AS_RF_SWITCH, &dio2_rf_switch, 1);

    // 4. Ustawienie typu modulacji na LoRa
    uint8_t pkt_type = 0x01; // LoRa
    sx1262_write_command(SX1262_CMD_SET_PACKET_TYPE, &pkt_type, 1);

    // 5. Ustawienie częstotliwości RF (wzór: FreqReg = (FreqHz * 2^25) / 32MHz)
    uint32_t freq_reg = (uint32_t)(((uint64_t)freq_hz * 33554432ULL) / 32000000ULL);
    uint8_t freq_buf[4];
    freq_buf[0] = (uint8_t)((freq_reg >> 24) & 0xFF);
    freq_buf[1] = (uint8_t)((freq_reg >> 16) & 0xFF);
    freq_buf[2] = (uint8_t)((freq_reg >> 8) & 0xFF);
    freq_buf[3] = (uint8_t)(freq_reg & 0xFF);
    sx1262_write_command(SX1262_CMD_SET_RF_FREQUENCY, freq_buf, 4);

    // 6. Konfiguracja bufora FIFO (baza TX: 0x00, baza RX: 0x00)
    uint8_t buf_base[2] = {0x00, 0x00};
    sx1262_write_command(SX1262_CMD_SET_BUFFER_BASE_ADDRESS, buf_base, 2);

    // 7. Parametry modulacji LoRa (SF, BW, CR, LowDataRateOptimize)
    uint8_t ldro = (sf >= 11 && bw == SX1262_LORA_BW_125) ? 0x01 : 0x00;
    uint8_t mod_params[4] = {sf, bw, cr, ldro};
    sx1262_write_command(SX1262_CMD_SET_MODULATION_PARAMS, mod_params, 4);

    // 8. Parametry pakietu (Preambuła 8 symboli, Nagłówek Explicit, MaxPayload 255, CRC ON, IQ Standard)
    uint8_t pkt_params[6] = {
        0x00, 0x08, // Preambuła: 8 symboli
        0x00,       // Header: Explicit (zmienna długość ramki)
        0xFF,       // Max Payload: 255 bajtów
        0x01,       // CRC: Włączone
        0x00        // InvertIQ: Standard
    };
    sx1262_write_command(SX1262_CMD_SET_PACKET_PARAMS, pkt_params, 6);

    // 9. Ustawienie LoRa Sync Word (0x1424 = sieć prywatna)
    sx1262_write_register(0x0740, (uint8_t)((sync_word >> 8) & 0xFF));
    sx1262_write_register(0x0741, (uint8_t)(sync_word & 0xFF));

    // 10. Konfiguracja przerwań: zmapowanie RxDone na linię DIO1
    uint8_t irq_buf[8];
    uint16_t irq_mask = SX1262_IRQ_RX_DONE | SX1262_IRQ_TIMEOUT | SX1262_IRQ_CRC_ERR;
    uint16_t dio1_mask = SX1262_IRQ_RX_DONE; // Pin DIO1 zmieni stan na HIGH po odebraniu ramki
    irq_buf[0] = (uint8_t)(irq_mask >> 8);
    irq_buf[1] = (uint8_t)(irq_mask & 0xFF);
    irq_buf[2] = (uint8_t)(dio1_mask >> 8);
    irq_buf[3] = (uint8_t)(dio1_mask & 0xFF);
    irq_buf[4] = 0x00; // DIO2
    irq_buf[5] = 0x00;
    irq_buf[6] = 0x00; // DIO3
    irq_buf[7] = 0x00;
    sx1262_write_command(SX1262_CMD_SET_DIO_IRQ_PARAMS, irq_buf, 8);

    // 11. Konfiguracja domyślnej mocy nadawania (+14 dBm)
    sx1262_set_tx_power(14);

    return true;
}

/**
 * Uruchomienie ciągłego nasłuchu (RX Continuous)
 */
bool sx1262_start_rx(void) {
    // 1. Włączenie toru odbiorczego na zewnętrznym przełączniku RF (RXEN = HIGH)
    digitalWrite(SX1262_PIN_RXEN, HIGH);

    // 2. Skonfigurowanie przerwań odbiornika: włączenie flagi RxDone
    uint8_t irq_buf[8];
    uint16_t irq_mask = SX1262_IRQ_RX_DONE | SX1262_IRQ_TIMEOUT | SX1262_IRQ_CRC_ERR | SX1262_IRQ_HEADER_VALID;
    uint16_t dio1_mask = SX1262_IRQ_RX_DONE;
    irq_buf[0] = (uint8_t)(irq_mask >> 8);
    irq_buf[1] = (uint8_t)(irq_mask & 0xFF);
    irq_buf[2] = (uint8_t)(dio1_mask >> 8);
    irq_buf[3] = (uint8_t)(dio1_mask & 0xFF);
    irq_buf[4] = 0x00;
    irq_buf[5] = 0x00;
    irq_buf[6] = 0x00;
    irq_buf[7] = 0x00;
    sx1262_write_command(SX1262_CMD_SET_DIO_IRQ_PARAMS, irq_buf, 8);

    // 3. Czyścimy stare flagi przerwań
    sx1262_clear_irq_status(SX1262_IRQ_ALL);

    // 4. Timeout 0xFFFFFF oznacza tryb ciągły (Continuous RX)
    uint8_t timeout_buf[3] = {0xFF, 0xFF, 0xFF};
    return sx1262_write_command(SX1262_CMD_SET_RX, timeout_buf, 3);
}

/**
 * Odczyt odebranego pakietu z bufora FIFO
 * Zwraca: liczbę odebranych bajtów, 0 jeśli brak pakietu, -1 jeśli błąd CRC
 */
int16_t sx1262_read_packet(uint8_t *payload, uint8_t max_len, float *rssi, float *snr) {
    uint16_t irq = sx1262_get_irq_status();

    // 1. Błąd sumy kontrolnej CRC
    if (irq & SX1262_IRQ_CRC_ERR) {
        sx1262_clear_irq_status(SX1262_IRQ_ALL);
        return -1;
    }

    // 2. Brak nowego pakietu
    if (!(irq & SX1262_IRQ_RX_DONE)) {
        return 0;
    }

    // 3. Odczyt długości i offsetu odebranego pakietu
    uint8_t rx_status[2] = {0};
    sx1262_read_command(SX1262_CMD_GET_RX_BUFFER_STATUS, rx_status, 2);
    uint8_t len = rx_status[0];
    uint8_t offset = rx_status[1];

    if (len > max_len) len = max_len;

    // 4. Odczyt danych z bufora FIFO
    sx1262_read_buffer(offset, payload, len);

    // 5. Odczyt parametrów jakości sygnału (RSSI i SNR)
    uint8_t pkt_status[3] = {0};
    sx1262_read_command(SX1262_CMD_GET_PACKET_STATUS, pkt_status, 3);
    if (rssi) {
        *rssi = -((float)pkt_status[0]) / 2.0f;
    }
    if (snr) {
        *snr = ((float)((int8_t)pkt_status[1])) / 4.0f;
    }

    // 6. Skasowanie przerwania RxDone
    sx1262_clear_irq_status(SX1262_IRQ_ALL);

    return len;
}

/**
 * Zapis danych do bufora FIFO układu SX1262 (OpCode 0x0E)
 */
bool sx1262_write_buffer(uint8_t offset, const uint8_t *data, uint8_t length) {
    sx1262_wait_busy();

    SPI.beginTransaction(spi_settings);
    digitalWrite(SX1262_PIN_NSS, LOW);

    SPI.transfer(SX1262_CMD_WRITE_BUFFER); // 0x0E
    SPI.transfer(offset);

    for (uint8_t i = 0; i < length; i++) {
        SPI.transfer(data[i]);
    }

    digitalWrite(SX1262_PIN_NSS, HIGH);
    SPI.endTransaction();

    sx1262_wait_busy();
    return true;
}

/**
 * Ustawienie mocy nadajnika RF (PA) w dBm (rekomendowane +14 dBm dla EU868)
 */
bool sx1262_set_tx_power(int8_t power_dbm) {
    // 1. Konfiguracja Power Amplifier (PA) dla układu SX1262 (+22 dBm max)
    uint8_t pa_config[4] = {
        0x04, // paDutyCycle
        0x07, // hpMax
        0x00, // deviceSel (0x00 = SX1262)
        0x01  // paLut
    };
    sx1262_write_command(SX1262_CMD_SET_PA_CONFIG, pa_config, 4);

    // 2. Ograniczenie wartości mocy do dopuszczalnych granic
    if (power_dbm > 22) power_dbm = 22;
    if (power_dbm < -9) power_dbm = -9;

    // 3. Ustawienie parametrów TX: moc + czas narastania zbocza (rampTime = 40us: 0x02)
    uint8_t tx_params[2] = {
        (uint8_t)power_dbm,
        0x02 // Ramp time 40 us
    };
    return sx1262_write_command(SX1262_CMD_SET_TX_PARAMS, tx_params, 2);
}

/**
 * Wysłanie ramki LoRa w eter
 */
bool sx1262_send_packet(const uint8_t *payload, uint8_t length, uint32_t timeout_ms) {
    // 0. Wyłączenie toru odbiorczego przełącznika RF (RXEN = LOW) przed nadawaniem
    digitalWrite(SX1262_PIN_RXEN, LOW);

    // 1. Wejście w tryb czuwania z aktywnym zegarem kwarcowym (STDBY_XOSC)
    uint8_t standby = 0x01; // STDBY_XOSC
    sx1262_write_command(SX1262_CMD_SET_STANDBY, &standby, 1);

    // 2. Ustawienie dokładnej długości wysyłanej ramki w PacketParams
    uint8_t pkt_params[6] = {
        0x00, 0x08, // Preambuła: 8 symboli
        0x00,       // Header: Explicit
        length,     // Długość ładunku do nadania
        0x01,       // CRC: Włączone
        0x00        // InvertIQ: Standard
    };
    sx1262_write_command(SX1262_CMD_SET_PACKET_PARAMS, pkt_params, 6);

    // 3. Załadowanie danych do bufora FIFO od adresu 0x00
    sx1262_write_buffer(0x00, payload, length);

    // 4. Skonfigurowanie przerwania TxDone na linii DIO1
    uint8_t irq_buf[8];
    uint16_t irq_mask = SX1262_IRQ_TX_DONE | SX1262_IRQ_TIMEOUT;
    uint16_t dio1_mask = SX1262_IRQ_TX_DONE;
    irq_buf[0] = (uint8_t)(irq_mask >> 8);
    irq_buf[1] = (uint8_t)(irq_mask & 0xFF);
    irq_buf[2] = (uint8_t)(dio1_mask >> 8);
    irq_buf[3] = (uint8_t)(dio1_mask & 0xFF);
    irq_buf[4] = 0x00;
    irq_buf[5] = 0x00;
    irq_buf[6] = 0x00;
    irq_buf[7] = 0x00;
    sx1262_write_command(SX1262_CMD_SET_DIO_IRQ_PARAMS, irq_buf, 8);

    // 5. Wyczyszczenie starych flag przerwań
    sx1262_clear_irq_status(SX1262_IRQ_ALL);

    // 6. Uruchomienie nadawania (OpCode SetTx: 0x83, timeout 0 = brak wewnętrznego limitu)
    uint8_t tx_timeout[3] = {0x00, 0x00, 0x00};
    sx1262_write_command(SX1262_CMD_SET_TX, tx_timeout, 3);

    // 7. Oczekiwanie na zakończenie emisji (flaga TxDone lub timeout w ms)
    uint32_t start_time = millis();
    while (true) {
        uint16_t irq = sx1262_get_irq_status();
        if (irq & SX1262_IRQ_TX_DONE) {
            // Sukces: pakiet wyleciał w eter
            sx1262_clear_irq_status(SX1262_IRQ_ALL);
            return true;
        }

        if (millis() - start_time >= timeout_ms) {
            Serial.println("[SX1262 BŁĄD] Timeout podczas nadawania pakietu!");
            sx1262_clear_irq_status(SX1262_IRQ_ALL);
            return false;
        }

        delayMicroseconds(500);
    }
}
