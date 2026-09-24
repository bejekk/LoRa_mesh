#include <Arduino.h>
#include "sx1262.h"
#include <SPI.h>

#define LED_PIN_USER LED_RED

void dump_read_register(uint16_t reg) {
    Serial.print("[DIAG] Odczyt rejestru 0x");
    Serial.print(reg, HEX);
    Serial.print(" - bajty z MISO: ");

    sx1262_wait_busy();

    SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
    digitalWrite(SX1262_PIN_NSS, LOW);

    uint8_t b0 = SPI.transfer(SX1262_CMD_READ_REGISTER); // 0x1D
    uint8_t b1 = SPI.transfer((uint8_t)((reg >> 8) & 0xFF));
    uint8_t b2 = SPI.transfer((uint8_t)(reg & 0xFF));
    uint8_t b3 = SPI.transfer(0x00); // NOP 1 (Status)
    uint8_t b4 = SPI.transfer(0x00); // Dane bajt 1?
    uint8_t b5 = SPI.transfer(0x00); // Dane bajt 2?

    digitalWrite(SX1262_PIN_NSS, HIGH);
    SPI.endTransaction();

    sx1262_wait_busy();

    Serial.print("b0=0x"); Serial.print(b0, HEX);
    Serial.print(" b1=0x"); Serial.print(b1, HEX);
    Serial.print(" b2=0x"); Serial.print(b2, HEX);
    Serial.print(" b3(Status)=0x"); Serial.print(b3, HEX);
    Serial.print(" b4=0x"); Serial.print(b4, HEX);
    Serial.print(" b5=0x"); Serial.println(b5, HEX);
}

void setup() {
    pinMode(LED_PIN_USER, OUTPUT);
    digitalWrite(LED_PIN_USER, HIGH);

    Serial.begin(115200);
    uint32_t start_time = millis();
    while (!Serial && (millis() - start_time < 3000)) delay(10);

    Serial.println("\n==================================================");
    Serial.println("   XIAO nRF52840 + SX1262: Szczegolowa diagnostyka SPI");
    Serial.println("==================================================");

    sx1262_init_io();
    sx1262_reset();

    sx1262_status_t st = sx1262_get_status();
    Serial.print("Status po resecie: 0x");
    Serial.println(st.raw_byte, HEX);

    // SetStandby RC
    uint8_t stdby = 0x00;
    sx1262_write_command(SX1262_CMD_SET_STANDBY, &stdby, 1);

    st = sx1262_get_status();
    Serial.print("Status po SetStandby(0x00): 0x");
    Serial.println(st.raw_byte, HEX);

    // SetPacketType LoRa (0x01)
    uint8_t pkt = 0x01;
    sx1262_write_command(SX1262_CMD_SET_PACKET_TYPE, &pkt, 1);

    st = sx1262_get_status();
    Serial.print("Status po SetPacketType(0x01): 0x");
    Serial.println(st.raw_byte, HEX);

    // Szczegółowy odczyt rejestrów 0x0740 i 0x0741
    dump_read_register(0x0740);
    dump_read_register(0x0741);

    // Test zapisu 0x14 do 0x0740 i 0x24 do 0x0741 (Standardowy Private SyncWord LoRa)
    Serial.println("\nZapisujemy 0x14 do 0x0740 i 0x24 do 0x0741...");
    sx1262_write_register(0x0740, 0x14);
    sx1262_write_register(0x0741, 0x24);

    dump_read_register(0x0740);
    dump_read_register(0x0741);
}

void loop() {
    delay(2000);
}
