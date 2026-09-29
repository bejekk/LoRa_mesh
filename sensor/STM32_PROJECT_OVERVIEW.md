# 🛰️ STM32WLE5JC (LoRa-E5 mini) Sensor Node — Project Overview & Architecture Guide

> **Dla Agentów AI i Programistów:** Ten plik zawiera kompletne podsumowanie architektury, konfiguracji sprzętowej, stosu narzędziowego oraz protokołu radiowego węzła czujnika opartego o układ **STM32WLE5JC** (płytka **Seeed Studio LoRa-E5 mini**). Przeczytaj ten dokument przed modyfikacją kodu w katalogu `sensor/`, aby uniknąć konieczności analizowania całego drzewa projektu.

---

## 1. 📌 Cel i Rola Komponentu

* **Rola w systemie:** Autonomiczny, bezprzewodowy węzeł telemetryczny (Czujnik Wilgotności Gleby).
* **Zadanie:** Cykliczny pomiar wilgotności i napięcia baterii, sformatowanie ramki JSON, wysłanie jej przez radio LoRa (868.0 MHz) do stacji bazowej (**Bramy** z katalogu `lora_bridge/`), odebranie potwierdzenia **ACK** w oknie czasowym (Stop-and-Wait ARQ) i przejście w tryb oszczędzania energii.
* **System operacyjny:** **FreeRTOS (CMSIS-RTOS v2)**.
* **Środowisko:** VS Code + STM32 VS Code Extension / CMake Tools + STM32CubeCLT.
* **Konfigurator peryferiów:** STM32CubeMX (`sensor.ioc`).

---

## 2. 🔌 Specyfikacja Sprzętowa i Pinout (LoRa-E5 mini)

Moduł integruje jednoukładowy mikrokontroler **STM32WLE5JCI6** (ARM Cortex-M4 @ 48 MHz, 256 KB Flash, 64 KB RAM) ze zintegrowanym rdzeniem radiowym Semtech SX126x.

### Tabela Wyprowadzeń i Przypisania Peryferiów:
| Pin MCU | Typ / Peryferium | Rola w projekcie | Uwagi sprzętowe |
| :--- | :--- | :--- | :--- |
| **`PA4`** | `GPIO_Output` (`LL`) | **`RF_SW_CTRL1`** | Sterowanie torem RX przełącznika antenowego RF Switch |
| **`PA5`** | `GPIO_Output` (`LL`) | **`RF_SW_CTRL2`** | Sterowanie torem TX przełącznika antenowego RF Switch |
| **`PB0`** | `PB0-VDD_TCXO` | **`VDDTCXO`** | Automatyczne zasilanie oscylatora TCXO 32 MHz z wewnętrznego LDO |
| **`PB5`** | `GPIO_Output` (`LL`) | **`LED_USER`** | Wbudowana dioda LED na płytce (aktywna stanem wysokim) |
| **`PB6`** | `USART1_TX` (`LL`) | Debug / Telemetria UART | Połączone z pokładowym konwerterem USB-C (115200 baud) |
| **`PB7`** | `USART1_RX` (`LL`) | Debug / Telemetria UART | Połączone z pokładowym konwerterem USB-C (115200 baud) |
| **`PB13`** | `GPIO_Input` | Przycisk Boot / User | Przycisk na płytce (zwiera do GND) |
| **`PA13`** | `SYS_JTMS-SWDIO` | Programowanie SWD | Domyślnie aktywne sprzętowo po resecie |
| **`PA14`** | `SYS_JTCK-SWCLK` | Programowanie SWD | Domyślnie aktywne sprzętowo po resecie |
| **Wewnętrzny** | `SUBGHZSPI` | Magistrala SPI do radia | Wewnętrzna szyna APB3 łącząca rdzeń M4 z modemem SX126x |
| **Wewnętrzny** | `SUBGHZ_Radio_IRQn` | Przerwanie radia (Exti 44) | Sygnalizacja `TxDone`, `RxDone`, `Timeout`, `CrcErr` (priorytet 5) |

### Logika Przełącznika Antenowego (RF Switch Truth Table):
Moduł LoRa-E5 wyprowadza tor nadawczy wyłącznie przez wyjście dużej mocy **RFO_HP** (High Power):
* **Odbiór (RX Mode):** `PA4 = 1`, `PA5 = 0`
* **Nadawanie (TX Mode, RFO_HP):** `PA4 = 0`, `PA5 = 1`
* **Wyłączony / Standby / Sleep:** `PA4 = 0`, `PA5 = 0`

---

## 3. ⚙️ Architektura Oprogramowania i Stos Technologiczny

### Dlaczego NIE używamy ST Middleware `SubGHz_Phy` ani ciężkiego HAL-a?
1. **Konflikt sekwencera ST z FreeRTOS:** Biblioteka `SubGHz_Phy` z CubeMX wymaga własnego sekwencera kooperacyjnego (`UTIL_SEQ`), który koliduje ze schedulerem FreeRTOS.
2. **Eliminacja blokad i narzutu pamięci:** Zwykły HAL używa blokujących pętli `HAL_GetTick()`, funkcji `__HAL_LOCK()` i zajmuje kilkadziesiąt KB pamięci Flash.
3. **Zastosowane rozwiązanie (Architektura Hybrydowa):**
   * **Inicjalizacja mikrokontrolera (Zegary, GPIO, USART1):** Wygenerowana przez STM32CubeMX na **sterownikach LL (Low-Layer)**. Funkcje LL to `__STATIC_INLINE`, kompilujące się do pojedynczych instrukcji procesora.
   * **Zegar bazowy HAL (Timebase):** Skonfigurowany na **`TIM1`** (lub TIM17). `SysTick` pozostaje w 100% dedykowany dla schedulera FreeRTOS.
   * **Sterownik radiowy:** Bezpośredni dostęp do rejestrów CMSIS magistrali `SUBGHZSPI` oraz rejestrów zasilania `PWR` (patrz sekcja 5).

---

## 4. 📻 Parametry Warstwy Fizycznej LoRa i Protokół

Wszystkie parametry radiowe **muszą być ściśle zsynchronizowane** z bramką w `lora_bridge/`:

* **Częstotliwość ($F_{RF}$):** `868000000 Hz` (868.0 MHz, EU868)
* **Modulacja:** LoRa
* **Spreading Factor (SF):** `SF7` (128 chipów/symbol)
* **Szerokość pasma (BW):** `125 kHz` (wartość rejestru: `0x04`)
* **Coding Rate (CR):** `4/5` (wartość rejestru: `0x01`)
* **Sync Word:** `0x1424` (rejestry modemu `0x0740` i `0x0741` – sieć prywatna LoRa)
* **Preambuła:** `8 symboli`
* **Nagłówek:** *Explicit Header* (długość ładunku i CRC w nagłówku)
* **Suma kontrolna (CRC):** Włączona sprzętowo
* **Moc wyjściowa:** `+14 dBm (25 mW)` na porcie RFO_HP

### Format Ramki Telemetrycznej (Czujnik $\rightarrow$ Brama):
Ciąg znaków JSON (ASCII):
```json
{"id":"plant_pot_1","hum":48.6,"vbat":4.06,"seq":3}
```

### Format Potwierdzenia (Brama $\rightarrow$ Czujnik):
Ciąg znaków tekstowych:
```text
ACK:<seq>
```
*Przykład:* `ACK:3`. Okno nasłuchu na ACK: max `1500 ms`.

---

## 5. 🛠️ Bezpośrednie Sterowanie Radiem przez CMSIS (Rejestry)

Modem radiowy w STM32WLE5 jest mapowany w przestrzeni pamięci jako peryferium SPI:
* **Chip Select (NSS):** bit `PWR_SUBGHZSPICR_NSS` w rejestrze `PWR->SUBGHZSPICR`:
  * Aktywacja (LOW): `PWR->SUBGHZSPICR &= ~PWR_SUBGHZSPICR_NSS;`
  * Deaktywacja (HIGH): `PWR->SUBGHZSPICR |= PWR_SUBGHZSPICR_NSS;`
* **Stan zajętości (BUSY):** flaga `PWR_SR2_RFBUSYS` w rejestrze `PWR->SR2`:
  * Oczekiwanie na gotowość: `while (PWR->SR2 & PWR_SR2_RFBUSYS);`
* **Wymiana danych SPI:** rejestr `SUBGHZSPI->DR`:
  * Nadanie i odbiór bajtu:
    ```c
    while (!(SUBGHZSPI->SR & SPI_SR_TXE));
    SUBGHZSPI->DR = data;
    while (!(SUBGHZSPI->SR & SPI_SR_RXNE));
    return (uint8_t)SUBGHZSPI->DR;
    ```
* **Kluczowa sekwencja zasilania TCXO:**
  Wysyłana jest komenda radiowa `SetTCXOMode (0x97)` z parametrami: napięcie `1.7V` (kod `0x01`) oraz timeout ~5 ms, a następnie radio przełączane jest w tryb `STDBY_XOSC (0x01)`.
* **Konfiguracja PA (High Power):**
  Komenda `SetPaConfig (0x95)` z parametrami `(0x02, 0x02, 0x00, 0x01)` dla mocy +14 dBm na wyjściu RFO_HP.

---

## 6. 🗂️ Struktura Katalogu `sensor/`

```text
sensor/
├── CMakeLists.txt              # Główny plik konfiguracyjny CMake
├── CMakePresets.json           # Presety budowania: "Debug" i "Release" (Ninja)
├── STM32WLE5XX_FLASH.ld        # Skrypt konsolidatora (Flash: 256KB, RAM: 64KB)
├── startup_stm32wle5xx.s       # Wektor przerwań i kod startowy ASM
├── sensor.ioc                  # Plik źródłowy projektu STM32CubeMX
├── cmake/
│   ├── gcc-arm-none-eabi.cmake # Konfiguracja kompilatora cross-gcc (ARM Cortex-M4)
│   └── stm32cubemx/            # Reguły budowania wygenerowane przez CubeMX
├── Core/
│   ├── Inc/
│   │   ├── FreeRTOSConfig.h    # Konfiguracja jądra FreeRTOS (Heap: 16KB)
│   │   ├── main.h              # Definicje pinów (LED_USER, RF_SW_CTRL1/2)
│   │   ├── gpio.h / usart.h    # Deklaracje peryferiów LL
│   │   └── subghz.h            # Deklaracje wewnętrznej magistrali radiowej
│   └── Src/
│       ├── main.c              # Inicjalizacja peryferiów i start jądra OS
│       ├── app_freertos.c      # Definicje zadań FreeRTOS (telemetryTask)
│       ├── gpio.c / usart.c    # Inicjalizacja GPIO i USART1 (LL)
│       ├── subghz.c            # Inicjalizacja zegara SUBGHZSPI i NVIC
│       ├── stm32wlxx_it.c      # Handlery przerwań (TIM1, SUBGHZ Radio)
│       └── syscalls.c          # Implementacja _write dla printf
├── Drivers/                    # Oficjalne biblioteki STM32WL HAL / LL / CMSIS
└── Middlewares/
    └── Third_Party/FreeRTOS/   # Źródła systemu operacyjnego FreeRTOS (CMSIS_V2)
```

---

## 7. 🔨 Jak Budować Projekt?

W systemie zainstalowany jest pakiet **STM32CubeCLT 1.22.0** (`C:\ST\STM32CubeCLT_1.22.0\`).

### Kompilacja z wiersza poleceń (PowerShell):
```powershell
cd c:\Users\Bejek\Desktop\projekt_czujnik_lora\LoRa_mesh\sensor
$env:PATH = "C:\ST\STM32CubeCLT_1.22.0\GNU-tools-for-STM32\bin;C:\ST\STM32CubeCLT_1.22.0\Ninja\bin;C:\ST\STM32CubeCLT_1.22.0\CMake\bin;" + $env:PATH
cmake --preset Debug
cmake --build --preset Debug
```
Wynikowy plik binarny: `build/Debug/sensor.elf` (oraz `.bin` / `.hex`).

### Kompilacja w VS Code:
Projekt jest automatycznie wykrywany przez rozszerzenie **CMake Tools** oraz **STM32 VS Code Extension**. Wystarczy wybrać zestaw narzędzi `arm-none-eabi` i kliknąć przycisk **Build** na dolnym pasku stanu.

---

## 8. 📋 Aktualny Status i Kolejne Kroki Implementacyjne

- [x] Konfiguracja mikrokontrolera STM32WLE5JC w CubeMX z bibliotekami LL.
- [x] Konfiguracja oscylatora TCXO 32 MHz na pinie `PB0-VDDTCXO`.
- [x] Konfiguracja peryferium USART1 (PB6/PB7) na potrzeby debugowania.
- [x] Konfiguracja przełącznika RF (PA4/PA5) i diody LED (PB5).
- [x] Sukces pierwszej kompilacji `sensor.elf` (Flash: ~20 KB, RAM: ~21 KB).
- [ ] **KROK 1:** Implementacja `__io_putchar` w `Core/Src/usart.c`, aby odblokować wyjście `printf()` na USB-C.
- [ ] **KROK 2:** Implementacja sterownika radiowego CMSIS (pliki `radio_e5.h` / `radio_e5.c`) z funkcjami:
  - `radio_init()`
  - `radio_send(payload, len)`
  - `radio_receive(buffer, max_len, timeout_ms, &rssi, &snr)`
- [ ] **KROK 3:** Implementacja zadania `telemetryTask` w `Core/Src/app_freertos.c` (pętla nadawanie $\rightarrow$ czekanie na ACK $\rightarrow$ sleep).
- [ ] **KROK 4:** Wgranie firmware na moduł przez ST-Link i weryfikacja dwukierunkowej łączności z bramką `lora_bridge`.
- [ ] **KROK 5:** Podłączenie przetwornika ADC / I2C właściwego czujnika wilgotności gleby i pomiaru napięcia baterii.
- [ ] **KROK 6:** Optymalizacja poboru energii (Tickless Idle FreeRTOS / tryb Stop2 między pomiarami).
