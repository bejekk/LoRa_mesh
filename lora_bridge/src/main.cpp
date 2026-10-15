#include <Arduino.h>
#include "sx1262.h"

#define ROLE_BRIDGE  1
#define ROLE_SENSOR  2

/* Active role selector */
#define CURRENT_ROLE ROLE_BRIDGE

/* Onboard user LED is active-low on XIAO nRF52840 */
#define LED_PIN_USER LED_RED

/* RF configuration (must match sensor settings) */
#define LORA_FREQUENCY_HZ     868000000UL
#define LORA_SPREADING_FACTOR SX1262_LORA_SF7
#define LORA_BANDWIDTH        SX1262_LORA_BW_125
#define LORA_CODING_RATE      SX1262_LORA_CR_4_5
#define LORA_SYNC_WORD        0x1424

#if CURRENT_ROLE == ROLE_BRIDGE

static uint32_t packets_received = 0;
static uint32_t last_heartbeat = 0;

void setup() {
    pinMode(LED_PIN_USER, OUTPUT);
    digitalWrite(LED_PIN_USER, HIGH);

    Serial.begin(115200);
    Serial1.begin(115200);

    /* Allow up to 2 seconds for USB CDC enumeration */
    uint32_t start_time = millis();
    while (!Serial && (millis() - start_time < 2000)) {
        delay(10);
    }

    Serial.println("[Bridge] Initializing XIAO nRF52840 + SX1262...");
    Serial1.println("[Bridge] Hardware UART ready (D6 TX, D7 RX)");

    sx1262_init_io();
    sx1262_reset();

    if (!sx1262_config_lora(LORA_FREQUENCY_HZ, LORA_SPREADING_FACTOR,
                            LORA_BANDWIDTH, LORA_CODING_RATE, LORA_SYNC_WORD)) {
        Serial.println("[Bridge] Error: SX1262 modem configuration failed");
    } else {
        Serial.println("[Bridge] Modem configured successfully");
    }

    sx1262_start_rx();
    Serial.println("[Bridge] Continuous RX enabled, listening for frames...");
}

void loop() {
    uint16_t irq = sx1262_get_irq_status();

    if ((irq & SX1262_IRQ_RX_DONE) || digitalRead(SX1262_PIN_DIO1) == HIGH) {
        uint8_t payload[256] = {0};
        float rssi = 0.0f;
        float snr = 0.0f;

        int16_t len = sx1262_read_packet(payload, sizeof(payload) - 1, &rssi, &snr);

        if (len > 0) {
            packets_received++;
            digitalWrite(LED_PIN_USER, LOW);

            payload[len] = '\0';

            /* Print telemetry to USB CDC and hardware UART */
            Serial.print("[Bridge] Rx #");
            Serial.print(packets_received);
            Serial.print(" (");
            Serial.print(len);
            Serial.print(" B, RSSI: ");
            Serial.print(rssi, 1);
            Serial.print(" dBm, SNR: ");
            Serial.print(snr, 1);
            Serial.print(" dB): ");
            Serial.println((char*)payload);

            Serial1.print("Payload: ");
            Serial1.println((char*)payload);

            /* Brief pause before ACK to allow node receiver turnaround */
            delay(20);

            char ack_msg[32];
            snprintf(ack_msg, sizeof(ack_msg), "ACK:%u", (unsigned int)packets_received);
            sx1262_send_packet((const uint8_t*)ack_msg, strlen(ack_msg), 1000);

            /* Return to listening mode */
            sx1262_start_rx();
            digitalWrite(LED_PIN_USER, HIGH);
        } else if (len == -1) {
            Serial.println("[Bridge] Frame CRC error");
            sx1262_start_rx();
        }
    }

    if (millis() - last_heartbeat >= 5000) {
        last_heartbeat = millis();
        sx1262_status_t st = sx1262_get_status();
        uint16_t current_irq = sx1262_get_irq_status();

        Serial.print("[Heartbeat] Status: 0x");
        Serial.print(st.raw_byte, HEX);
        Serial.print(" | IRQ: 0x");
        Serial.print(current_irq, HEX);
        Serial.print(" | Total Rx: ");
        Serial.println(packets_received);

        Serial1.print("[Heartbeat] Total Rx: ");
        Serial1.println(packets_received);
    }
}

#elif CURRENT_ROLE == ROLE_SENSOR

static uint32_t packet_seq = 0;

void setup() {
    pinMode(LED_PIN_USER, OUTPUT);
    digitalWrite(LED_PIN_USER, HIGH);

    Serial.begin(115200);
    uint32_t start_time = millis();
    while (!Serial && (millis() - start_time < 3000)) {
        delay(10);
    }

    Serial.println("[Sensor] Initializing XIAO nRF52840 sensor node...");

    sx1262_init_io();
    sx1262_reset();

    if (!sx1262_config_lora(LORA_FREQUENCY_HZ, LORA_SPREADING_FACTOR,
                            LORA_BANDWIDTH, LORA_CODING_RATE, LORA_SYNC_WORD)) {
        Serial.println("[Sensor] Error: SX1262 configuration failed");
    } else {
        Serial.println("[Sensor] Modem configured successfully");
    }
}

void loop() {
    packet_seq++;
    digitalWrite(LED_PIN_USER, LOW);

    /* Simulated readings matching server schema */
    float simulated_humidity = 45.0f + (packet_seq % 20) * 1.2f;
    float simulated_voltage = 4.12f - (packet_seq % 10) * 0.02f;

    char telemetry[80];
    snprintf(telemetry, sizeof(telemetry),
             "{\"id\":\"plant_pot_1\",\"hum\":%.1f,\"vbat\":%.2f,\"seq\":%u}",
             simulated_humidity, simulated_voltage, (unsigned int)packet_seq);

    Serial.print("[Sensor] Transmitting seq #");
    Serial.print(packet_seq);
    Serial.print(": ");
    Serial.println(telemetry);

    bool tx_ok = sx1262_send_packet((const uint8_t*)telemetry, strlen(telemetry), 1500);

    if (tx_ok) {
        /* Listen for bridge ACK (1.5s timeout) */
        sx1262_start_rx();
        uint32_t rx_start = millis();
        bool ack_received = false;

        while (millis() - rx_start < 1500) {
            uint16_t irq = sx1262_get_irq_status();
            if ((irq & SX1262_IRQ_RX_DONE) || digitalRead(SX1262_PIN_DIO1) == HIGH) {
                uint8_t rx_buf[64] = {0};
                float rssi, snr;
                int16_t len = sx1262_read_packet(rx_buf, sizeof(rx_buf) - 1, &rssi, &snr);
                if (len > 0) {
                    rx_buf[len] = '\0';
                    Serial.print("[Sensor] ACK received: ");
                    Serial.println((char*)rx_buf);
                    ack_received = true;
                    break;
                }
            }
            delay(10);
        }

        if (!ack_received) {
            Serial.println("[Sensor] ACK timeout");
        }
    } else {
        Serial.println("[Sensor] Transmission failed");
    }

    digitalWrite(LED_PIN_USER, HIGH);

    /* Place radio into low-power standby between cycles */
    uint8_t stdby = 0x00;
    sx1262_write_command(SX1262_CMD_SET_STANDBY, &stdby, 1);

    delay(5000);
}

#endif
