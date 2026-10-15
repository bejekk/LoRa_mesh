#include "sx1262.h"
#include <Arduino.h>
#include <SPI.h>

static SPISettings spi_settings(2000000, MSBFIRST, SPI_MODE0);

#define SX1262_MAX_RETRIES 3

void sx1262_init_io(void) {
    pinMode(SX1262_PIN_NSS, OUTPUT);
    digitalWrite(SX1262_PIN_NSS, HIGH);

    pinMode(SX1262_PIN_RESET, OUTPUT);
    digitalWrite(SX1262_PIN_RESET, HIGH);

    pinMode(SX1262_PIN_BUSY, INPUT);
    pinMode(SX1262_PIN_DIO1, INPUT);

    pinMode(SX1262_PIN_RXEN, OUTPUT);
    digitalWrite(SX1262_PIN_RXEN, LOW);

    SPI.begin();
}

void sx1262_wait_busy(void) {
    uint32_t start_time = millis();
    while (digitalRead(SX1262_PIN_BUSY) == HIGH) {
        if (millis() - start_time > 100) {
            Serial.println("[SX1262] Timeout waiting for BUSY deassertion");
            break;
        }
    }
}

void sx1262_reset(void) {
    digitalWrite(SX1262_PIN_RESET, LOW);
    delayMicroseconds(200); /* Min 100 us per datasheet */
    digitalWrite(SX1262_PIN_RESET, HIGH);
    delay(10);
    sx1262_wait_busy();
}

bool sx1262_write_command(uint8_t opcode, const uint8_t *data, uint16_t length) {
    for (uint8_t attempt = 0; attempt < SX1262_MAX_RETRIES; attempt++) {
        sx1262_wait_busy();

        SPI.beginTransaction(spi_settings);
        digitalWrite(SX1262_PIN_NSS, LOW);

        SPI.transfer(opcode);
        for (uint16_t i = 0; i < length; i++) {
            SPI.transfer(data[i]);
        }

        digitalWrite(SX1262_PIN_NSS, HIGH);
        SPI.endTransaction();

        sx1262_wait_busy();
        return true;
    }
    return false;
}

bool sx1262_read_command(uint8_t opcode, uint8_t *data, uint16_t length) {
    for (uint8_t attempt = 0; attempt < SX1262_MAX_RETRIES; attempt++) {
        sx1262_wait_busy();

        SPI.beginTransaction(spi_settings);
        digitalWrite(SX1262_PIN_NSS, LOW);

        SPI.transfer(opcode);
        uint8_t status_byte = SPI.transfer(0x00);
        uint8_t cmd_status = (status_byte >> 1) & 0x07;

        /* 0x03 = command timeout, 0x04 = command processing error */
        if (cmd_status == 0x03 || cmd_status == 0x04) {
            digitalWrite(SX1262_PIN_NSS, HIGH);
            SPI.endTransaction();

            char err_buf[64];
            snprintf(err_buf, sizeof(err_buf), "[SX1262] Opcode 0x%02X error 0x%02X (attempt %u)\n",
                     opcode, cmd_status, attempt + 1);
            Serial.print(err_buf);

            sx1262_wait_busy();
            delayMicroseconds(500);
            continue;
        }

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

sx1262_status_t sx1262_get_status(void) {
    sx1262_wait_busy();

    SPI.beginTransaction(spi_settings);
    digitalWrite(SX1262_PIN_NSS, LOW);

    uint8_t raw = SPI.transfer(SX1262_CMD_GET_STATUS);
    uint8_t status_byte = SPI.transfer(0x00);

    digitalWrite(SX1262_PIN_NSS, HIGH);
    SPI.endTransaction();

    uint8_t final_byte = (status_byte != 0x00 && status_byte != 0xFF) ? status_byte : raw;

    sx1262_status_t status;
    status.raw_byte = final_byte;
    status.chip_mode = (final_byte >> 4) & 0x07;
    status.cmd_status = (final_byte >> 1) & 0x07;
    return status;
}

bool sx1262_write_register(uint16_t reg, uint8_t value) {
    uint8_t buf[3];
    buf[0] = (uint8_t)((reg >> 8) & 0xFF);
    buf[1] = (uint8_t)(reg & 0xFF);
    buf[2] = value;
    return sx1262_write_command(SX1262_CMD_WRITE_REGISTER, buf, 3);
}

bool sx1262_read_register_checked(uint16_t reg, uint8_t *value) {
    for (uint8_t attempt = 0; attempt < SX1262_MAX_RETRIES; attempt++) {
        sx1262_wait_busy();

        SPI.beginTransaction(spi_settings);
        digitalWrite(SX1262_PIN_NSS, LOW);

        SPI.transfer(SX1262_CMD_READ_REGISTER);
        SPI.transfer((uint8_t)((reg >> 8) & 0xFF));
        SPI.transfer((uint8_t)(reg & 0xFF));

        uint8_t status_byte = SPI.transfer(0x00);
        uint8_t cmd_status = (status_byte >> 1) & 0x07;

        if (cmd_status == 0x03 || cmd_status == 0x04) {
            digitalWrite(SX1262_PIN_NSS, HIGH);
            SPI.endTransaction();
            sx1262_wait_busy();
            delayMicroseconds(500);
            continue;
        }

        *value = SPI.transfer(0x00);

        digitalWrite(SX1262_PIN_NSS, HIGH);
        SPI.endTransaction();

        sx1262_wait_busy();
        return true;
    }
    return false;
}

uint8_t sx1262_read_register(uint16_t reg) {
    uint8_t val = 0;
    sx1262_read_register_checked(reg, &val);
    return val;
}

bool sx1262_read_buffer(uint8_t offset, uint8_t *data, uint8_t length) {
    sx1262_wait_busy();

    SPI.beginTransaction(spi_settings);
    digitalWrite(SX1262_PIN_NSS, LOW);

    SPI.transfer(SX1262_CMD_READ_BUFFER);
    SPI.transfer(offset);
    SPI.transfer(0x00); /* Dummy status byte */

    for (uint8_t i = 0; i < length; i++) {
        data[i] = SPI.transfer(0x00);
    }

    digitalWrite(SX1262_PIN_NSS, HIGH);
    SPI.endTransaction();

    sx1262_wait_busy();
    return true;
}

uint16_t sx1262_get_irq_status(void) {
    uint8_t buf[2] = {0};
    sx1262_read_command(SX1262_CMD_GET_IRQ_STATUS, buf, 2);
    return ((uint16_t)buf[0] << 8) | buf[1];
}

void sx1262_clear_irq_status(uint16_t mask) {
    uint8_t buf[2];
    buf[0] = (uint8_t)((mask >> 8) & 0xFF);
    buf[1] = (uint8_t)(mask & 0xFF);
    sx1262_write_command(SX1262_CMD_CLEAR_IRQ_STATUS, buf, 2);
}

bool sx1262_config_lora(uint32_t freq_hz, uint8_t sf, uint8_t bw, uint8_t cr, uint16_t sync_word) {
    uint8_t standby = 0x00;
    sx1262_write_command(SX1262_CMD_SET_STANDBY, &standby, 1);

    uint8_t reg_mode = 0x01; /* DCDC mode */
    sx1262_write_command(SX1262_CMD_SET_REGULATOR_MODE, &reg_mode, 1);

    /* TCXO 1.7 V on DIO3 with 5 ms startup delay (0x0140 = 320 * 15.625 us) */
    uint8_t tcxo_buf[4] = {0x01, 0x00, 0x01, 0x40};
    sx1262_write_command(SX1262_CMD_SET_DIO3_AS_TCXO_CTRL, tcxo_buf, 4);

    uint8_t stdby_xosc = 0x01;
    sx1262_write_command(SX1262_CMD_SET_STANDBY, &stdby_xosc, 1);
    delay(10);

    uint8_t dio2_rf_switch = 0x01;
    sx1262_write_command(SX1262_CMD_SET_DIO2_AS_RF_SWITCH, &dio2_rf_switch, 1);

    uint8_t pkt_type = 0x01; /* LoRa */
    sx1262_write_command(SX1262_CMD_SET_PACKET_TYPE, &pkt_type, 1);

    uint32_t freq_reg = (uint32_t)(((uint64_t)freq_hz * 33554432ULL) / 32000000ULL);
    uint8_t freq_buf[4] = {
        (uint8_t)(freq_reg >> 24),
        (uint8_t)(freq_reg >> 16),
        (uint8_t)(freq_reg >> 8),
        (uint8_t)(freq_reg)
    };
    sx1262_write_command(SX1262_CMD_SET_RF_FREQUENCY, freq_buf, 4);

    uint8_t buf_base[2] = {0x00, 0x00};
    sx1262_write_command(SX1262_CMD_SET_BUFFER_BASE_ADDRESS, buf_base, 2);

    uint8_t ldro = (sf >= 11 && bw == SX1262_LORA_BW_125) ? 0x01 : 0x00;
    uint8_t mod_params[4] = {sf, bw, cr, ldro};
    sx1262_write_command(SX1262_CMD_SET_MODULATION_PARAMS, mod_params, 4);

    /* 8 preamble symbols, explicit header, 255 max payload, CRC on, standard IQ */
    uint8_t pkt_params[6] = {0x00, 0x08, 0x00, 0xFF, 0x01, 0x00};
    sx1262_write_command(SX1262_CMD_SET_PACKET_PARAMS, pkt_params, 6);

    sx1262_write_register(0x0740, (uint8_t)((sync_word >> 8) & 0xFF));
    sx1262_write_register(0x0741, (uint8_t)(sync_word & 0xFF));

    /* Route RxDone to DIO1 */
    uint8_t irq_buf[8] = {
        (uint8_t)((SX1262_IRQ_RX_DONE | SX1262_IRQ_TIMEOUT | SX1262_IRQ_CRC_ERR) >> 8),
        (uint8_t)(SX1262_IRQ_RX_DONE | SX1262_IRQ_TIMEOUT | SX1262_IRQ_CRC_ERR),
        (uint8_t)(SX1262_IRQ_RX_DONE >> 8),
        (uint8_t)SX1262_IRQ_RX_DONE,
        0x00, 0x00, 0x00, 0x00
    };
    sx1262_write_command(SX1262_CMD_SET_DIO_IRQ_PARAMS, irq_buf, 8);

    sx1262_set_tx_power(14);
    return true;
}

bool sx1262_start_rx(void) {
    digitalWrite(SX1262_PIN_RXEN, HIGH);

    uint8_t irq_buf[8] = {
        (uint8_t)((SX1262_IRQ_RX_DONE | SX1262_IRQ_TIMEOUT | SX1262_IRQ_CRC_ERR | SX1262_IRQ_HEADER_VALID) >> 8),
        (uint8_t)(SX1262_IRQ_RX_DONE | SX1262_IRQ_TIMEOUT | SX1262_IRQ_CRC_ERR | SX1262_IRQ_HEADER_VALID),
        (uint8_t)(SX1262_IRQ_RX_DONE >> 8),
        (uint8_t)SX1262_IRQ_RX_DONE,
        0x00, 0x00, 0x00, 0x00
    };
    sx1262_write_command(SX1262_CMD_SET_DIO_IRQ_PARAMS, irq_buf, 8);
    sx1262_clear_irq_status(SX1262_IRQ_ALL);

    /* 0xFFFFFF = Continuous RX */
    uint8_t timeout_buf[3] = {0xFF, 0xFF, 0xFF};
    return sx1262_write_command(SX1262_CMD_SET_RX, timeout_buf, 3);
}

int16_t sx1262_read_packet(uint8_t *payload, uint8_t max_len, float *rssi, float *snr) {
    uint16_t irq = sx1262_get_irq_status();

    if (irq & SX1262_IRQ_CRC_ERR) {
        sx1262_clear_irq_status(SX1262_IRQ_ALL);
        return -1;
    }

    if (!(irq & SX1262_IRQ_RX_DONE)) {
        return 0;
    }

    uint8_t rx_status[2] = {0};
    sx1262_read_command(SX1262_CMD_GET_RX_BUFFER_STATUS, rx_status, 2);
    uint8_t len = rx_status[0];
    uint8_t offset = rx_status[1];

    if (len > max_len) {
        len = max_len;
    }

    sx1262_read_buffer(offset, payload, len);

    uint8_t pkt_status[3] = {0};
    sx1262_read_command(SX1262_CMD_GET_PACKET_STATUS, pkt_status, 3);
    if (rssi) {
        *rssi = -((float)pkt_status[0]) / 2.0f;
    }
    if (snr) {
        *snr = ((float)((int8_t)pkt_status[1])) / 4.0f;
    }

    sx1262_clear_irq_status(SX1262_IRQ_ALL);
    return len;
}

bool sx1262_write_buffer(uint8_t offset, const uint8_t *data, uint8_t length) {
    sx1262_wait_busy();

    SPI.beginTransaction(spi_settings);
    digitalWrite(SX1262_PIN_NSS, LOW);

    SPI.transfer(SX1262_CMD_WRITE_BUFFER);
    SPI.transfer(offset);
    for (uint8_t i = 0; i < length; i++) {
        SPI.transfer(data[i]);
    }

    digitalWrite(SX1262_PIN_NSS, HIGH);
    SPI.endTransaction();

    sx1262_wait_busy();
    return true;
}

bool sx1262_set_tx_power(int8_t power_dbm) {
    uint8_t pa_config[4] = {0x04, 0x07, 0x00, 0x01};
    sx1262_write_command(SX1262_CMD_SET_PA_CONFIG, pa_config, 4);

    if (power_dbm > 22) power_dbm = 22;
    if (power_dbm < -9) power_dbm = -9;

    uint8_t tx_params[2] = {(uint8_t)power_dbm, 0x02}; /* 40 us ramp */
    return sx1262_write_command(SX1262_CMD_SET_TX_PARAMS, tx_params, 2);
}

bool sx1262_send_packet(const uint8_t *payload, uint8_t length, uint32_t timeout_ms) {
    digitalWrite(SX1262_PIN_RXEN, LOW);

    uint8_t standby = 0x01; /* STDBY_XOSC */
    sx1262_write_command(SX1262_CMD_SET_STANDBY, &standby, 1);

    uint8_t pkt_params[6] = {0x00, 0x08, 0x00, length, 0x01, 0x00};
    sx1262_write_command(SX1262_CMD_SET_PACKET_PARAMS, pkt_params, 6);

    sx1262_write_buffer(0x00, payload, length);

    uint8_t irq_buf[8] = {
        (uint8_t)((SX1262_IRQ_TX_DONE | SX1262_IRQ_TIMEOUT) >> 8),
        (uint8_t)(SX1262_IRQ_TX_DONE | SX1262_IRQ_TIMEOUT),
        (uint8_t)(SX1262_IRQ_TX_DONE >> 8),
        (uint8_t)SX1262_IRQ_TX_DONE,
        0x00, 0x00, 0x00, 0x00
    };
    sx1262_write_command(SX1262_CMD_SET_DIO_IRQ_PARAMS, irq_buf, 8);
    sx1262_clear_irq_status(SX1262_IRQ_ALL);

    uint8_t tx_timeout[3] = {0x00, 0x00, 0x00};
    sx1262_write_command(SX1262_CMD_SET_TX, tx_timeout, 3);

    uint32_t start_time = millis();
    while (true) {
        uint16_t irq = sx1262_get_irq_status();
        if (irq & SX1262_IRQ_TX_DONE) {
            sx1262_clear_irq_status(SX1262_IRQ_ALL);
            return true;
        }

        if (millis() - start_time >= timeout_ms) {
            sx1262_clear_irq_status(SX1262_IRQ_ALL);
            return false;
        }
        delayMicroseconds(500);
    }
}
