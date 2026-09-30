#!/usr/bin/env bash
set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="$DIR/build/Debug/sensor.bin"
ELF="$DIR/build/Debug/sensor.elf"

if [ ! -f "$ELF" ]; then
    echo "Kompilowanie projektu..."
    cmake --build "$DIR/build/Debug"
fi

echo "=== Wgrywanie firmware do Seeed LoRa-E5 (STM32WLE5JC) ==="

# 1. Próba STM32_Programmer_CLI (jeśli zainstalowane z CubeIDE / CLT)
STM32_PROG="/opt/st/stm32cubeide/plugins/com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.linux64_2.2.0.202409170845/tools/bin/STM32_Programmer_CLI"
if [ -x "$STM32_PROG" ]; then
    echo "--> [1/3] Próba przez STM32_Programmer_CLI..."
    if "$STM32_PROG" -c port=SWD mode=UR -w "$ELF" -v -rst; then
        echo "--> Sukces! Program wgrany poprawnie przez STM32_Programmer_CLI."
        exit 0
    fi
fi

# 2. Próba OpenOCD
if command -v openocd &> /dev/null; then
    echo "--> [2/3] Próba przez OpenOCD (ST-Link)..."
    if openocd -f interface/stlink.cfg -f target/stm32wlx.cfg -c "program $ELF verify reset exit"; then
        echo "--> Sukces! Program wgrany poprawnie przez OpenOCD."
        exit 0
    fi
fi

# 3. Próba st-flash
if command -v st-flash &> /dev/null; then
    echo "--> [3/3] Próba przez st-flash..."
    if st-flash write "$BIN" 0x08000000; then
        echo "--> Sukces! Program wgrany poprawnie przez st-flash."
        exit 0
    fi
fi

echo "BŁĄD: Nie udało się wgrać programu."
exit 1

