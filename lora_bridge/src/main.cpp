#include <Arduino.h>
#include "sx1262.h"

/**
 * ============================================================================
 * Projekt: LoRa Mesh / Point-to-Point (XIAO nRF52840 + Wio-SX1262)
 * Komunikacja dwukierunkowa z potwierdzeniem (ACK)
 * ============================================================================
 * 
 * WYBÓR ROLI URZĄDZENIA:
 * Odkomentuj tylko JEDNĄ z poniższych linii przed wgraniem na płytkę:
 */
#define ROLE_BRIDGE  1   // Brama: nasłuchuje, wypisuje na UART i odsyła potwierdzenie (ACK)
#define ROLE_SENSOR  2   // Czujnik: wysyła pomiary, czeka na ACK, symuluje uśpienie

// <<< TUTAJ WYBIERASZ ROLĘ DLA TEJ PŁYTKI >>>
// #define CURRENT_ROLE ROLE_BRIDGE
#define CURRENT_ROLE ROLE_SENSOR

#define LED_PIN_USER LED_RED

// Parametry radiowe (muszą być identyczne na obu płytkach!)
#define LORA_FREQUENCY_HZ     868000000UL // 868.0 MHz
#define LORA_SPREADING_FACTOR SX1262_LORA_SF7
#define LORA_BANDWIDTH        SX1262_LORA_BW_125
#define LORA_CODING_RATE      SX1262_LORA_CR_4_5
#define LORA_SYNC_WORD        0x1424      // Prywatna sieć LoRa

#if CURRENT_ROLE == ROLE_BRIDGE

/**
 * ============================================================================
 * KOD DLA BRAMY (BRIDGE / GATEWAY)
 * ============================================================================
 */
static uint32_t packets_received = 0;
static uint32_t last_heartbeat = 0;

void setup() {
    pinMode(LED_PIN_USER, OUTPUT);
    digitalWrite(LED_PIN_USER, HIGH); // Zgaszona (odwrócona logika)

    Serial.begin(115200);
    uint32_t start_time = millis();
    while (!Serial && (millis() - start_time < 3000)) delay(10);

    Serial.println("\n=======================================================");
    Serial.println("   XIAO nRF52840 + Wio-SX1262: ROLA -> MOST (BRIDGE)");
    Serial.println("   (Nasluchuje na radio, odsyla ACK i wysyla na UART)");
    Serial.println("=======================================================");

    sx1262_init_io();
    sx1262_reset();

    if (sx1262_config_lora(LORA_FREQUENCY_HZ, LORA_SPREADING_FACTOR, LORA_BANDWIDTH, LORA_CODING_RATE, LORA_SYNC_WORD)) {
        Serial.println("[OK] Modem LoRa skonfigurowany pomyslnie.");
    } else {
        Serial.println("[BLAD] Konfiguracja modemu nie powiodla sie!");
    }

    // Uruchomienie ciągłego nasłuchu
    sx1262_start_rx();
    Serial.println("[OK] Nasluch RX wlaczony. Czekam na ramki od czujnikow...\n");
}

void loop() {
    // 1. Sprawdzenie czy przyszedł pakiet (przez rejestr IRQ po SPI lub pin DIO1)
    uint16_t irq = sx1262_get_irq_status();

    if ((irq & SX1262_IRQ_RX_DONE) || digitalRead(SX1262_PIN_DIO1) == HIGH) {
        uint8_t payload[256] = {0};
        float rssi = 0.0f;
        float snr = 0.0f;

        int16_t len = sx1262_read_packet(payload, sizeof(payload) - 1, &rssi, &snr);

        if (len > 0) {
            packets_received++;
            digitalWrite(LED_PIN_USER, LOW); // Włącz LED

            payload[len] = '\0'; // Bezpieczny string

            Serial.println("-------------------------------------------------------");
            Serial.print("[BRAMA] Odebrano pakiet #");
            Serial.print(packets_received);
            Serial.print(" (");
            Serial.print(len);
            Serial.println(" B)");
            Serial.print("  Sygnal: RSSI = ");
            Serial.print(rssi, 1);
            Serial.print(" dBm | SNR = ");
            Serial.print(snr, 1);
            Serial.println(" dB");
            Serial.print("  Dane (UART -> RPi): ");
            Serial.println((char*)payload);

            // 2. Natychmiastowe odesłanie potwierdzenia ACK do czujnika
            delay(20); // Krótka pauza na ustabilizowanie radia
            char ack_msg[32];
            snprintf(ack_msg, sizeof(ack_msg), "ACK:%u", (unsigned int)packets_received);

            Serial.print("  -> Odsylam potwierdzenie: ");
            Serial.println(ack_msg);

            sx1262_send_packet((const uint8_t*)ack_msg, strlen(ack_msg), 1000);

            // 3. Powrót do ciągłego nasłuchu w eterze
            sx1262_start_rx();

            Serial.println("-------------------------------------------------------");
            digitalWrite(LED_PIN_USER, HIGH); // Wyłącz LED
        } else if (len == -1) {
            Serial.println("[OSTRZEZENIE] Blad sumy CRC ramki!");
            sx1262_start_rx();
        }
    }

    // Heartbeat co 5s
    if (millis() - last_heartbeat >= 5000) {
        last_heartbeat = millis();
        sx1262_status_t st = sx1262_get_status();
        uint16_t current_irq = sx1262_get_irq_status();
        Serial.print("[Heartbeat Brama] Stan: 0x");
        Serial.print(st.raw_byte, HEX);
        Serial.print(" | IRQ: 0x");
        Serial.print(current_irq, HEX);
        Serial.print(" | Odebrano: ");
        Serial.println(packets_received);
    }
}

#elif CURRENT_ROLE == ROLE_SENSOR

/**
 * ============================================================================
 * KOD DLA CZUJNIKA (SENSOR NODE)
 * ============================================================================
 */
static uint32_t packet_seq = 0;

void setup() {
    pinMode(LED_PIN_USER, OUTPUT);
    digitalWrite(LED_PIN_USER, HIGH);

    Serial.begin(115200);
    uint32_t start_time = millis();
    while (!Serial && (millis() - start_time < 3000)) delay(10);

    Serial.println("\n=======================================================");
    Serial.println("   XIAO nRF52840 + Wio-SX1262: ROLA -> CZUJNIK (SENSOR)");
    Serial.println("   (Wysyla pomiary, czeka na ACK z bramy i idzie spac)");
    Serial.println("=======================================================");

    sx1262_init_io();
    sx1262_reset();

    if (sx1262_config_lora(LORA_FREQUENCY_HZ, LORA_SPREADING_FACTOR, LORA_BANDWIDTH, LORA_CODING_RATE, LORA_SYNC_WORD)) {
        Serial.println("[OK] Modem LoRa skonfigurowany pomyslnie.\n");
    } else {
        Serial.println("[BLAD] Konfiguracja modemu nie powiodla sie!\n");
    }
}

void loop() {
    packet_seq++;
    digitalWrite(LED_PIN_USER, LOW); // Włącz LED

    // 1. Przygotowanie przykładowych danych pomiarowych (format pasujący do bazy danych serwera)
    float simulated_humidity = 45.0f + (packet_seq % 20) * 1.2f;
    float simulated_voltage = 4.12f - (packet_seq % 10) * 0.02f;

    char telemetry[80];
    snprintf(telemetry, sizeof(telemetry), "{\"id\":\"plant_pot_1\",\"hum\":%.1f,\"vbat\":%.2f,\"seq\":%u}",
             simulated_humidity, simulated_voltage, (unsigned int)packet_seq);

    Serial.println("-------------------------------------------------------");
    Serial.print("[CZUJNIK] Nadawanie pomiaru #");
    Serial.println(packet_seq);
    Serial.print("  Wysylam: ");
    Serial.println(telemetry);

    // 2. Wysłanie ramki w eter
    uint32_t tx_start = millis();
    bool tx_ok = sx1262_send_packet((const uint8_t*)telemetry, strlen(telemetry), 1500);
    Serial.print("  Czas emisji: ");
    Serial.print(millis() - tx_start);
    Serial.println(" ms");

    if (!tx_ok) {
        Serial.println("  [BLAD] Nadawanie nie powiodlo sie!");
    } else {
        // 3. Natychmiastowe przejście w nasłuch i oczekiwanie na potwierdzenie ACK (max 1.5 sekundy)
        Serial.println("  [RX ACK] Czekam na potwierdzenie z bramy (max 1.5s)...");
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
                    Serial.print("  >>> SUKCES: Odebrano potwierdzenie! Odpowiedz: ");
                    Serial.println((char*)rx_buf);
                    Serial.print("      (Czas reakcji: ");
                    Serial.print(millis() - rx_start);
                    Serial.print(" ms | RSSI: ");
                    Serial.print(rssi, 1);
                    Serial.println(" dBm)");
                    ack_received = true;
                    break;
                }
            }
            delay(10);
        }

        if (!ack_received) {
            Serial.println("  [TIMEOUT] Brak potwierdzenia ACK od bramy!");
        }
    }

    digitalWrite(LED_PIN_USER, HIGH); // Wyłącz LED

    // 4. Przełączenie radia w Standby przed uśpieniem
    uint8_t stdby = 0x00;
    sx1262_write_command(SX1262_CMD_SET_STANDBY, &stdby, 1);

    Serial.println("  [SEN] Czujnik zasypia na 5 sekund...");
    Serial.println("-------------------------------------------------------");

    // W finalnym urządzeniu tutaj wołasz deep sleep mikrokontrolera
    delay(5000);
}

#endif
