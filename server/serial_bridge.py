import serial
import json
import re
import sys
import time
from database import add_measurement, init_db

# Domyślny port szeregowy dla mostka XIAO nRF52840
SERIAL_PORT = "/dev/ttyACM0"
BAUD_RATE = 115200

def run_bridge(port=SERIAL_PORT, baud=BAUD_RATE):
    """
    Nasłuchuje na porcie szeregowym, odbiera pakiety wypisane przez mostek LoRa
    i automatycznie zapisuje je do bazy SQLite na Raspberry Pi.
    """
    init_db()
    print(f"--- Uruchomiono mostek UART -> SQLite na porcie {port} ({baud} bps) ---")
    print("Oczekiwanie na ramki LoRa z czujnikow...")

    while True:
        try:
            with serial.Serial(port, baud, timeout=1) as ser:
                print(f"[OK] Polaczono z urzadzeniem na {port}")
                while True:
                    line = ser.readline().decode('utf-8', errors='ignore').strip()
                    if not line:
                        continue

                    # Wykrywanie JSON w linii z mostka
                    # np. 'Dane (UART -> RPi): {"id":"plant_pot_1","hum":48.6,"vbat":4.06,"seq":3}'
                    json_match = re.search(r'\{.*\}', line)
                    if json_match:
                        try:
                            data = json.loads(json_match.group(0))
                            sensor_id = data.get("id", "unknown")
                            humidity = float(data.get("hum", 0.0))
                            voltage = float(data.get("vbat", 0.0))
                            seq = data.get("seq", 0)

                            add_measurement(
                                sensor_id=sensor_id,
                                humidity=humidity,
                                battery_voltage=voltage
                            )
                            print(f"[ZAPISANO] #{seq} | Czujnik: {sensor_id} | Wilgotnosc: {humidity}% | Bateria: {voltage}V")
                        except Exception as parse_err:
                            print(f"[BLAD PARSOWANIA] {parse_err} w linii: {line}")
                    elif "[Heartbeat" in line:
                        print(f"[STATUS] {line}")

        except serial.SerialException as e:
            print(f"[BLAD POLACZENIA] Nie mozna polaczyc z {port}: {e}. Ponawiam za 3 sekundy...")
            time.sleep(3)
        except KeyboardInterrupt:
            print("\nZatrzymano mostek UART.")
            break

if __name__ == "__main__":
    port_arg = sys.argv[1] if len(sys.argv) > 1 else SERIAL_PORT
    run_bridge(port=port_arg)
