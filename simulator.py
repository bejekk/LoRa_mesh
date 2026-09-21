import time
import random
from database import add_measurement, init_db

# Domyślne parametry symulacji
SENSOR_ID = "plant_pot_1"
INTERVAL_SECONDS = 10

def generate_simulated_data():
    """Generuje realistyczne, losowe dane z czujnika."""
    # Wilgotność gleby w zakresie 30.0% - 75.0%
    humidity = round(random.uniform(30.0, 75.0), 1)
    
    # Napięcie baterii Li-Ion/LiPo w zakresie 3.60V - 4.20V
    battery_voltage = round(random.uniform(3.60, 4.20), 2)
    
    return humidity, battery_voltage

def run_simulation(interval=INTERVAL_SECONDS, sensor_id=SENSOR_ID, max_iterations=None):
    """
    Główna pętla symulacji.
    max_iterations: opcjonalny limit obiegów (przydatny do automatycznych testów).
    """
    # Upewniamy się, że baza danych istnieje przed rozpoczęciem zapisu
    init_db()
    
    print(f"--- Uruchomiono symulator danych dla sensora: '{sensor_id}' ---")
    print(f"Interwał wysyłania: {interval} sekund")
    print("Naciśnij Ctrl+C, aby zatrzymać symulację.\n")

    iteration = 0
    try:
        while True:
            humidity, battery_voltage = generate_simulated_data()
            
            # Zapis do bazy danych
            add_measurement(
                sensor_id=sensor_id,
                humidity=humidity,
                battery_voltage=battery_voltage
            )
            
            print(f"[ZAPISANO] Sensor: {sensor_id} | Wilgotność: {humidity}% | Bateria: {battery_voltage}V")
            
            iteration += 1
            if max_iterations is not None and iteration >= max_iterations:
                print(f"\nOsiągnięto limit {max_iterations} iteracji. Zakończenie testu.")
                break
                
            time.sleep(interval)

    except KeyboardInterrupt:
        print("\nSymulator zatrzymany ręcznie przez użytkownika (Ctrl+C).")

if __name__ == "__main__":
    run_simulation(interval=INTERVAL_SECONDS, sensor_id=SENSOR_ID)

