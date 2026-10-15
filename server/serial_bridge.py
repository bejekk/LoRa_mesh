import glob
import json
import os
import re
import sys
import time
from typing import Optional
import serial

from database import add_measurement, init_db

DEFAULT_PORT = "/dev/serial0"
BAUD_RATE = 115200

def find_serial_port(preferred: str = DEFAULT_PORT) -> str:
    """Detect available USB CDC/ACM, FTDI or Raspberry Pi GPIO UART ports."""
    candidates = glob.glob("/dev/ttyACM*") + glob.glob("/dev/ttyUSB*")
    for gpio_port in ["/dev/serial0", "/dev/ttyS0", "/dev/ttyAMA0"]:
        if os.path.exists(gpio_port) and gpio_port not in candidates:
            candidates.append(gpio_port)

    if preferred in candidates:
        return preferred
    return candidates[0] if candidates else preferred

def run_bridge(port: Optional[str] = None, baud: int = BAUD_RATE) -> None:
    init_db()
    print(f"[Bridge] Starting UART telemetry listener (baud: {baud})...")

    while True:
        target_port = port if port else find_serial_port()
        try:
            with serial.Serial(target_port, baud, timeout=1) as ser:
                print(f"[Bridge] Connected to {target_port}")
                while True:
                    line = ser.readline().decode("utf-8", errors="ignore").strip()
                    if not line:
                        continue

                    # Extract JSON telemetry payload
                    json_match = re.search(r"\{.*\}", line)
                    if json_match:
                        try:
                            # Sanitize null values if MCU emitted incomplete fields
                            raw_json = json_match.group(0)
                            sanitized = re.sub(r":\s*,", ":null,", raw_json)
                            sanitized = re.sub(r":\s*\}", ":null}", sanitized)

                            data = json.loads(sanitized)
                            sensor_id = str(data.get("id", "unknown"))
                            seq = int(data.get("seq", 0))

                            hum_raw = data.get("hum")
                            vbat_raw = data.get("vbat")

                            humidity = float(hum_raw) if hum_raw is not None else 0.0
                            voltage = float(vbat_raw) if vbat_raw is not None else 0.0

                            add_measurement(
                                sensor_id=sensor_id,
                                humidity=humidity,
                                battery_voltage=voltage
                            )
                            print(f"[Bridge] Stored seq #{seq} | ID: {sensor_id} | Soil: {humidity:.1f}% | Vbat: {voltage:.2f}V")
                        except Exception as parse_err:
                            print(f"[Bridge] Parse error: {parse_err} in '{line}'")
                    elif "[Heartbeat" in line:
                        print(f"[Bridge] {line}")

        except serial.SerialException as e:
            print(f"[Bridge] Serial error on {target_port}: {e}. Retrying in 3s...")
            time.sleep(3)
        except KeyboardInterrupt:
            print("\n[Bridge] Stopping listener.")
            break

if __name__ == "__main__":
    port_arg = sys.argv[1] if len(sys.argv) > 1 else None
    run_bridge(port=port_arg)
