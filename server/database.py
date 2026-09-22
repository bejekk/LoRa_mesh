import sqlite3
from datetime import datetime, timezone
from pathlib import Path

# 1. Dynamiczna ścieżka do bazy - działa tak samo na Windows i na Raspberry Pi
DB_PATH = Path(__file__).resolve().parent / "sensors.db"

def _get_connection():
    """
    Wewnętrzna funkcja pomocnicza do tworzenia połączenia z bazą.
    Nazwa zaczyna się od podłogi (_), co w Pythonie oznacza, że funkcja
    powinna być używana tylko wewnątrz tego pliku.
    """
    # Łączymy się z bazą (plik zostanie utworzony automatycznie, jeśli nie istnieje)
    conn = sqlite3.connect(DB_PATH)
    
    # Zmieniamy domyślny format zwracanych danych na taki, który zachowuje się jak słownik
    conn.row_factory = sqlite3.Row
    
    # Włączamy tryb WAL (Write-Ahead Logging)
    conn.execute("PRAGMA journal_mode=WAL;")
    
    return conn

def init_db():
    """Tworzy strukturę bazy danych, jeśli ta jeszcze nie istnieje."""
    with _get_connection() as conn:
        # Tworzymy tabelę pomiarów
        conn.execute("""
            CREATE TABLE IF NOT EXISTS measurements (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                timestamp TEXT NOT NULL,
                sensor_id TEXT NOT NULL,
                humidity REAL NOT NULL,
                battery_voltage REAL NOT NULL
            )
        """)
        
        # Tworzymy indeks na kolumnie timestamp, by wyszukiwanie i sortowanie 
        # po dacie (np. na wykresie) było błyskawiczne, nawet przy milionach rekordów
        conn.execute("""
            CREATE INDEX IF NOT EXISTS idx_timestamp 
            ON measurements (timestamp)
        """)

def add_measurement(sensor_id, humidity, battery_voltage, timestamp=None):
    """Dodaje nowy pomiar do bazy danych."""
    # Jeśli czujnik nie wysłał własnego czasu, ustawiamy aktualny czas serwera (w strefie UTC)
    if timestamp is None:
        timestamp = datetime.now(timezone.utc).isoformat()

    with _get_connection() as conn:
        # Używamy znaków zapytania (?) zamiast wklejać zmienne prosto do tekstu SQL
        conn.execute("""
            INSERT INTO measurements (timestamp, sensor_id, humidity, battery_voltage)
            VALUES (?, ?, ?, ?)
        """, (timestamp, sensor_id, humidity, battery_voltage))
        # 'with' automatycznie robi tu conn.commit(), zapisując dane na stałe

def get_latest_measurement(sensor_id=None):
    """Pobiera najświeższy pojedynczy pomiar z bazy."""
    with _get_connection() as conn:
        if sensor_id:
            cursor = conn.execute("""
                SELECT * FROM measurements 
                WHERE sensor_id = ? 
                ORDER BY timestamp DESC 
                LIMIT 1
            """, (sensor_id,))
        else:
            cursor = conn.execute("""
                SELECT * FROM measurements 
                ORDER BY timestamp DESC 
                LIMIT 1
            """)
        
        row = cursor.fetchone()
        # Zwracamy wynik jako standardowy słownik Pythona (lub None, jeśli baza jest pusta)
        return dict(row) if row else None

def get_history(limit=100, sensor_id=None):
    """Pobiera historię pomiarów do narysowania wykresu."""
    with _get_connection() as conn:
        # Trik SQL: Najpierw pobieramy N najnowszych rekordów (żeby nie zapchać pamięci),
        # a potem sortujemy je rosnąco (ASC), by na wykresie czas płynął od lewej do prawej.
        if sensor_id:
            query = """
                SELECT * FROM (
                    SELECT * FROM measurements 
                    WHERE sensor_id = ? 
                    ORDER BY timestamp DESC 
                    LIMIT ?
                ) ORDER BY timestamp ASC
            """
            cursor = conn.execute(query, (sensor_id, limit))
        else:
            query = """
                SELECT * FROM (
                    SELECT * FROM measurements 
                    ORDER BY timestamp DESC 
                    LIMIT ?
                ) ORDER BY timestamp ASC
            """
            cursor = conn.execute(query, (limit,))
            
        # Przekształcamy wszystkie znalezione wiersze na listę słowników
        return [dict(row) for row in cursor.fetchall()]


if __name__ == "__main__":
    print("--- Test modułu database.py ---")
    print(f"Lokalizacja bazy danych: {DB_PATH}")

    # 1. Inicjalizacja bazy
    print("Inicjalizacja bazy danych...")
    init_db()

    # 2. Dodanie przykładowych pomiarów
    print("Dodawanie testowych pomiarów...")
    add_measurement(sensor_id="flower_pot_1", humidity=45.5, battery_voltage=3.95)
    add_measurement(sensor_id="flower_pot_1", humidity=44.8, battery_voltage=3.94)
    add_measurement(sensor_id="flower_pot_2", humidity=60.2, battery_voltage=4.10)

    # 3. Odczyt najnowszego pomiaru
    latest = get_latest_measurement()
    print(f"Najnowszy pomiar (dowolny czujnik): {latest}")

    latest_pot1 = get_latest_measurement(sensor_id="flower_pot_1")
    print(f"Najnowszy pomiar (flower_pot_1): {latest_pot1}")

    # 4. Odczyt historii
    history = get_history(limit=5)
    print(f"Historia (ostatnie {len(history)} rekordy):")
    for item in history:
        print(f"  [{item['timestamp']}] Sensor: {item['sensor_id']} | Wilgotność: {item['humidity']}% | Bateria: {item['battery_voltage']}V")

    print("--- Test zakończony sukcesem! ---")
