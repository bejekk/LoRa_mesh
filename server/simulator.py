import random
import time
from typing import Optional, Tuple

from database import add_measurement, init_db

SENSOR_ID = "plant_pot_1"
INTERVAL_SECONDS = 10

def generate_simulated_data() -> Tuple[float, float]:
    """Generate realistic soil moisture (30-75%) and LiPo battery voltage (3.6-4.2V)."""
    humidity = round(random.uniform(30.0, 75.0), 1)
    battery_voltage = round(random.uniform(3.60, 4.20), 2)
    return humidity, battery_voltage

def run_simulation(
    interval: int = INTERVAL_SECONDS,
    sensor_id: str = SENSOR_ID,
    max_iterations: Optional[int] = None
) -> None:
    init_db()
    print(f"[Simulator] Running for sensor '{sensor_id}' (interval: {interval}s)...")

    iteration = 0
    try:
        while True:
            humidity, battery_voltage = generate_simulated_data()
            add_measurement(
                sensor_id=sensor_id,
                humidity=humidity,
                battery_voltage=battery_voltage
            )
            print(f"[Simulator] Stored: {sensor_id} | Soil: {humidity}% | Battery: {battery_voltage}V")

            iteration += 1
            if max_iterations is not None and iteration >= max_iterations:
                print(f"[Simulator] Reached limit of {max_iterations} iterations.")
                break

            time.sleep(interval)
    except KeyboardInterrupt:
        print("\n[Simulator] Stopped by user.")

if __name__ == "__main__":
    run_simulation()
